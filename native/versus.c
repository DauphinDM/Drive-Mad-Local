// Read-only restoration of the ORIGINAL asynchronous Versus mode. No new
// matchmaking protocol and no uploads. All engine calls stay on the main
// thread.
#include "gameplay-profile.h"
#include "host.h"
#include <curl/curl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define MAX_FETCH (2u * 1024u * 1024u)
#define MAX_INFLATE (32u * 1024u * 1024u)
#define PREFIX "https://api.drivemad.com/vs/5/"
typedef struct {
  CURL *easy;
  unsigned char *bytes;
  size_t size;
  u32 id;
  bool used, done, ok, replay;
} Fetch;
struct Versus {
  CURLM *multi;
  Fetch fetches[8];
  bool online;
  char cache[4096], status[256];
};

static const char *filename(const char *url) {
  if (strncmp(url, PREFIX, sizeof(PREFIX) - 1))
    return NULL;
  const char *name = url + sizeof(PREFIX) - 1;
  size_t n = strlen(name);
  if (!strcmp(name, "manifest.data"))
    return name;
  if (n < 10 || n > 127 || strncmp(name, "rec_", 4) ||
      strcmp(name + n - 5, ".data"))
    return NULL;
  for (size_t i = 0; i < n - 5; i++)
    if (!((name[i] >= 'a' && name[i] <= 'z') ||
          (name[i] >= 'A' && name[i] <= 'Z') ||
          (name[i] >= '0' && name[i] <= '9') || name[i] == '_'))
      return NULL;
  return name;
}

static size_t receive(char *bytes, size_t size, size_t count, void *userdata) {
  Fetch *f = userdata;
  if (size && count > MAX_FETCH / size)
    return 0;
  size_t n = size * count;
  if (n > MAX_FETCH - f->size)
    return 0;
  unsigned char *p = realloc(f->bytes, f->size + n);
  if (!p && n)
    return 0;
  f->bytes = p;
  if (n)
    memcpy(p + f->size, bytes, n);
  f->size += n;
  return n;
}

typedef struct {
  const unsigned char *p;
  size_t left;
  bool bad;
} Cursor;
static const unsigned char *take(Cursor *c, size_t n) {
  if (n > c->left) {
    c->bad = true;
    return NULL;
  }
  const unsigned char *p = c->p;
  c->p += n;
  c->left -= n;
  return p;
}
static u32 be32(Cursor *c) {
  const unsigned char *p = take(c, 4);
  return p ? (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3] : 0;
}
static unsigned be16(Cursor *c) {
  const unsigned char *p = take(c, 2);
  return p ? (unsigned)p[0] << 8 | p[1] : 0;
}
static bool replay_string(Cursor *c) {
  unsigned n = be16(c);
  const unsigned char *p = take(c, n + 1);
  return n && n <= 16 && p && !p[n] && !memchr(p, 0, n);
}
static bool pose_valid(Cursor *c, bool delta) {
  for (int i = 0; i < 4; i++) {
    if (!delta) {
      u32 bits = be32(c);
      float value;
      memcpy(&value, &bits, 4);
      if (c->bad || !isfinite(value) || fabsf(value) > 1e7f)
        return false;
    } else {
      unsigned n = 0;
      const unsigned char *p;
      do {
        if (n++ == 5 || !(p = take(c, 1)))
          return false;
      } while (*p & 128);
    }
  }
  return true;
}
// Stock v9 decoder allocates from counts embedded in the replay. Validate its
// complete wire structure first; a small compressed file mustn't request giant
// arrays, trigger modulo-zero, or fall through truncated varint reads.
static bool replay_valid(const unsigned char *p, size_t n) {
  if (n < 21 || memcmp(p, "dma\0\0\x09", 6))
    return false;
  Cursor c = {.p = p + 6, .left = n - 6};
  if (be32(&c) != n - 10 || !take(&c, 11) || !replay_string(&c) ||
      !replay_string(&c))
    return false;
  u32 tags = be32(&c);
  if (tags > 64 || !take(&c, tags * 5))
    return false;
  be32(&c); // original run seed
  u32 frames = be32(&c), win = be32(&c), loss = be32(&c);
  u32 parts = be32(&c), sources = be32(&c), interval = be32(&c);
  u32 bits = be32(&c);
  float scale;
  memcpy(&scale, &bits, 4);
  if (c.bad || !frames || frames > 54000 || !win || win > frames ||
      (loss != UINT32_MAX && loss > frames) || !parts || parts > 6 ||
      !sources || sources > 4096 || !interval || interval > 54000 ||
      !isfinite(scale) || scale <= 0 || scale > 1)
    return false;
  for (u32 frame = 0; frame < frames; frame++)
    for (u32 part = 0; part < parts; part++)
      if (!pose_valid(&c, frame % interval != 0))
        return false;
  u32 events = be32(&c), seen[4096] = {0};
  if (events > 100000)
    return false;
  for (u32 i = 0; i < events; i++) {
    unsigned source = be16(&c);
    if (source >= sources || !pose_valid(&c, seen[source]++ % interval != 0))
      return false;
  }
  u32 actions = be32(&c);
  return !c.bad && actions <= 100000 && take(&c, (size_t)actions * 12) &&
         !c.left;
}

// Bound inflated responses and preflight v9 replay allocations. The original
// JSON/replay parsers are still legacy game code, not a sandbox.
// TLS verification remains enabled; redirects and arbitrary URLs aren't.
static bool compressed_valid(const Fetch *f) {
  z_stream z = {0};
  if (!f->size || inflateInit(&z) != Z_OK)
    return false;
  unsigned char out[16384];
  unsigned char *replay = f->replay ? malloc(MAX_INFLATE) : NULL;
  if (f->replay && !replay) {
    inflateEnd(&z);
    return false;
  }
  z.next_in = f->bytes;
  z.avail_in = (uInt)f->size;
  int result;
  do {
    z.next_out = out;
    z.avail_out = sizeof(out);
    result = inflate(&z, Z_NO_FLUSH);
    if (z.total_out > MAX_INFLATE) {
      inflateEnd(&z);
      free(replay);
      return false;
    }
    if (replay)
      memcpy(replay + z.total_out - (sizeof(out) - z.avail_out), out,
             sizeof(out) - z.avail_out);
  } while (result == Z_OK);
  bool ok = result == Z_STREAM_END && z.avail_in == 0;
  if (ok && replay)
    ok = replay_valid(replay, z.total_out);
  inflateEnd(&z);
  free(replay);
  return ok;
}

bool versus_init(struct w2c_a *h, const char *cache, bool online) {
  if ((!DM_GAMEPLAY_PROFILE && (cache || online)) || h->versus)
    return false;
  struct Versus *v = calloc(1, sizeof(*v));
  if (!v)
    return false;
  if (cache && strlen(cache) >= sizeof(v->cache)) {
    free(v);
    return false;
  }
  if (cache)
    strcpy(v->cache, cache);
  if (online) {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
      free(v);
      return false;
    }
    v->multi = curl_multi_init();
    if (!v->multi) {
      curl_global_cleanup();
      free(v);
      return false;
    }
  }
  v->online = online;
  snprintf(v->status, sizeof(v->status),
           "Original Versus: %s; uploads disabled.",
           online  ? "read-only online replays"
           : cache ? "local replay cache"
                   : "offline");
  h->versus = v;
  return true;
}

void versus_enter(struct w2c_a *h) {
#if DM_GAMEPLAY_PROFILE
  if (!h->versus || (!h->versus->online && !h->versus->cache[0]) ||
      !mods_ready(h))
    return;
  // Enter the game's existing scene/state transition, not an invented spawn.
  game_versus_enter(&h->game);
  fprintf(stderr, "%s\n", h->versus->status);
#else
  (void)h;
#endif
}

void versus_fetch(struct w2c_a *h, const char *url, u32 request, bool upload) {
  struct Versus *v = h->versus;
  const char *name = upload ? NULL : filename(url);
  if (!v)
    fail("Native request pump is not initialized");
  Fetch *f = NULL;
  for (int i = 0; i < 8; i++)
    if (!v->fetches[i].used) {
      f = &v->fetches[i];
      break;
    }
  if (!f)
    fail("Native request pump exceeded the core's eight-request limit");
  *f = (Fetch){.used = true, .id = request};
  if (!name || (!v->online && !v->cache[0])) {
    // Defer failures, too: the engine is still dispatching its request here.
    f->done = true;
    fprintf(stderr, "Versus: blocked %s\n",
            upload ? "upload" : "offline/non-replay URL");
    return;
  }
  f->replay = strcmp(name, "manifest.data") != 0;
  if (!v->online) {
    char path[4240];
    snprintf(path, sizeof(path), "%s/%s", v->cache, name);
    FILE *file = fopen(path, "rb");
    if (file) {
      unsigned char chunk[16384];
      size_t n;
      f->ok = true;
      while ((n = fread(chunk, 1, sizeof(chunk), file)))
        if (receive((char *)chunk, 1, n, f) != n) {
          f->ok = false;
          break;
        }
      if (ferror(file))
        f->ok = false;
      fclose(file);
    }
    f->done = true;
    return;
  }
  f->easy = curl_easy_init();
  if (!f->easy) {
    f->done = true;
    return;
  }
  curl_easy_setopt(f->easy, CURLOPT_URL, url);
  curl_easy_setopt(f->easy, CURLOPT_PROTOCOLS_STR, "https");
  curl_easy_setopt(f->easy, CURLOPT_FOLLOWLOCATION, 0L);
  curl_easy_setopt(f->easy, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
  curl_easy_setopt(f->easy, CURLOPT_TIMEOUT_MS, 15000L);
  curl_easy_setopt(f->easy, CURLOPT_NOSIGNAL, 1L);
#if (defined(_WIN32) || defined(__APPLE__)) && defined(CURLSSLOPT_NATIVE_CA)
  // Packaged builds must use the user's trust store, not MSYS's build-time
  // path.
  curl_easy_setopt(f->easy, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
#endif
  curl_easy_setopt(f->easy, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(f->easy, CURLOPT_USERAGENT,
                   "DriveMad-Native/1 (replay downloads)");
  curl_easy_setopt(f->easy, CURLOPT_WRITEFUNCTION, receive);
  curl_easy_setopt(f->easy, CURLOPT_WRITEDATA, f);
  curl_easy_setopt(f->easy, CURLOPT_PRIVATE, f);
  if (curl_multi_add_handle(v->multi, f->easy) != CURLM_OK)
    f->done = true;
}

void versus_tick(struct w2c_a *h) {
  struct Versus *v = h->versus;
  if (!v)
    return;
  if (v->multi) {
    int running, remaining;
    curl_multi_perform(v->multi, &running);
    CURLMsg *message;
    while ((message = curl_multi_info_read(v->multi, &remaining))) {
      Fetch *f = NULL;
      curl_easy_getinfo(message->easy_handle, CURLINFO_PRIVATE, &f);
      if (f && message->msg == CURLMSG_DONE) {
        long code = 0;
        curl_easy_getinfo(f->easy, CURLINFO_RESPONSE_CODE, &code);
        f->ok = message->data.result == CURLE_OK && code == 200;
        f->done = true;
      }
    }
  }
  for (int i = 0; i < 8; i++) {
    Fetch *f = &v->fetches[i];
    if (!f->used || !f->done)
      continue;
    if (f->easy) {
      curl_multi_remove_handle(v->multi, f->easy);
      curl_easy_cleanup(f->easy);
    }
    bool ok = f->ok && compressed_valid(f);
    u32 data = ok ? game_malloc(&h->game, (u32)f->size) : 0;
    if (data)
      memcpy(mem(h, data, f->size), f->bytes, f->size);
    snprintf(
        v->status, sizeof(v->status),
        "Original Versus: replay request %s (%zu bytes). Uploads disabled.",
        data ? "received" : "failed", f->size);
    fprintf(stderr, "%s\n", v->status);
    // Engine owns data after callback, and frees it in its request pump.
    game_web_command_fetch_send_url_done(&h->game, data,
                                         data ? (u32)f->size : 0, f->id);
    free(f->bytes);
    *f = (Fetch){0};
  }
}

const char *versus_status(struct w2c_a *h) {
  return h->versus ? h->versus->status
                   : "Original Versus offline. Launch with --versus to enable "
                     "replay downloads.";
}

bool versus_enable_online(struct w2c_a *h) {
  if (!DM_GAMEPLAY_PROFILE)
    return false;
  if (h->versus && h->versus->online)
    return true;
  if (h->versus)
    for (int i = 0; i < 8; i++)
      if (h->versus->fetches[i].used)
        return false;
  versus_close(h);
  return versus_init(h, NULL, true);
}

void versus_close(struct w2c_a *h) {
  struct Versus *v = h->versus;
  if (!v)
    return;
  for (int i = 0; i < 8; i++) {
    Fetch *f = &v->fetches[i];
    if (f->easy) {
      curl_multi_remove_handle(v->multi, f->easy);
      curl_easy_cleanup(f->easy);
    }
    free(f->bytes);
  }
  if (v->multi) {
    curl_multi_cleanup(v->multi);
    curl_global_cleanup();
  }
  free(v);
  h->versus = NULL;
}
