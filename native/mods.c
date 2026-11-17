// Trusted, opt-in native extensions. The API is checked, not a process sandbox.
#include "host.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include "win32_compat.h"
#else
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define MAX_MODS 64
#define MAX_BLOB (16u * 1024u * 1024u)
typedef struct {
  char key[64], value[1024];
} Entry;
typedef struct {
  Entry entries[64];
  size_t count;
} Ini;
typedef struct {
  const unsigned char *data;
  size_t size;
  bool resolved, owned;
} Asset;
struct DMContext {
  struct w2c_a *host;
  char id[64], name[128], version[64], library[128],
    requires[512];
  int directory, data_directory, library_fd;
  uint32_t game_version;
  bool enabled, loaded;
  Ini *settings;
  void *library_handle, *userdata;
  const DMMod *descriptor;
};
typedef struct {
  char sender[64], topic[64];
  unsigned char data[4096];
  size_t size;
} Message;
struct ModHost {
  DMContext mods[MAX_MODS];
  DMContext builtin;
  uint32_t count;
  bool ready, console;
  double uptime;
  DMFrame frame;
  Asset *assets;
  size_t override_bytes;
  Message messages[64];
  size_t message_count;
  char input[4096];
  size_t input_size;
  bool discard_line;
};

static bool identifier(const char *s) {
  size_t n = s ? strlen(s) : 0;
  return n && n < 64 && s[0] != '.' && !strstr(s, "..") &&
         strspn(s, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456"
                   "789_.-") == n;
}
static char *trim(char *s) {
  while (isspace((unsigned char)*s))
    s++;
  size_t n = strlen(s);
  while (n && isspace((unsigned char)s[n - 1]))
    s[--n] = 0;
  return s;
}
static const char *get(const Ini *ini, const char *key) {
  for (size_t i = 0; i < ini->count; i++)
    if (!strcmp(key, ini->entries[i].key))
      return ini->entries[i].value;
  return NULL;
}
static void read_ini(int dir, const char *name, Ini *ini, bool optional) {
  int fd = openat(dir, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
  if (fd < 0 && optional && errno == ENOENT)
    return;
  struct stat st;
  if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size > 65536)
    fail("Mod INI must be a regular file of at most 64 KiB");
  FILE *file = fdopen(fd, "r");
  if (!file)
    fail("Cannot read mod INI");
  char line[2048];
  while (fgets(line, sizeof(line), file)) {
    if (!strchr(line, '\n') && !feof(file))
      fail("Mod INI line too long");
    char *key = trim(line);
    if (!*key || *key == '#' || *key == ';')
      continue;
    char *equals = strchr(key, '=');
    if (!equals)
      fail("Mod INI expects key=value (no sections)");
    *equals = 0;
    key = trim(key);
    char *value = trim(equals + 1);
    if (!identifier(key) || strlen(value) >= 1024 || get(ini, key) ||
        ini->count == 64)
      fail("Duplicate, invalid, or excessive mod INI entry");
    Entry *entry = &ini->entries[ini->count++];
    strcpy(entry->key, key);
    strcpy(entry->value, value);
  }
  if (ferror(file))
    fail("Mod INI read failed");
  fclose(file);
}
static void copy_field(char *dest, size_t capacity, const char *value,
                       const char *fallback) {
  if (!value)
    value = fallback;
  if (!value || strlen(value) >= capacity)
    fail("Missing or oversized mod manifest field");
  strcpy(dest, value);
}
static DMStatus read_blob(int fd, void *buffer, size_t capacity, size_t *size) {
  struct stat st;
  if (!size || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 ||
      st.st_size > MAX_BLOB)
    return DM_EIO;
  *size = (size_t)st.st_size;
  if (!buffer && !capacity)
    return DM_OK;
  if (!buffer)
    return DM_EINVAL;
  if (capacity < *size)
    return DM_EBOUNDS;
  size_t done = 0;
  while (done < *size) {
    ssize_t n = pread(fd, (char *)buffer + done, *size - done, done);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      return DM_EIO;
    done += (size_t)n;
  }
  return DM_OK;
}
static const char *status_string(DMStatus s) {
  switch (s) {
  case DM_OK:
    return "ok";
  case DM_EINVAL:
    return "invalid argument";
  case DM_ENOTFOUND:
    return "not found";
  case DM_EBOUNDS:
    return "out of bounds";
  case DM_ENOTREADY:
    return "game not ready";
  case DM_EIO:
    return "I/O failure";
  case DM_EABI:
    return "signature/ABI mismatch";
  case DM_EFULL:
    return "queue full";
  case DM_EPROTECTED:
    return "protected operation / unsupported game profile";
  default:
    return "unknown error";
  }
}
static void api_log(DMContext *c, const char *text) {
  fprintf(stderr, "[mod:%s] %s\n", c->id, text ? text : "(null)");
  menu_log(c->host, c->id, text ? text : "(null)");
}
bool mods_ready(struct w2c_a *h) { return h->mods && h->mods->ready; }
static DMStatus api_state(DMContext *c, DMState *out) {
  if (!out || out->struct_size != sizeof(*out))
    return DM_EABI;
  struct w2c_a *h = c->host;
  *out = (DMState){.struct_size = sizeof(*out),
                   .game_version = game_get_app_version(&h->game),
                   .ready = mods_ready(h),
                   .paused = h->paused,
                   .width = h->width,
                   .height = h->height,
                   .frames = h->frames,
                   .draw_calls = h->draw_calls,
                   .audio_buffers = h->audio_buffers,
                   .asset_reads = h->asset_reads,
                   .input_events = h->input_events,
                   .uptime_seconds = h->mods->uptime};
  return DM_OK;
}
static uint32_t api_export_count(DMContext *c) {
  return dm_export_count + gameplay_export_count;
}
static const DMExport *api_export_at(DMContext *c, uint32_t index) {
  if (index < dm_export_count)
    return &dm_exports[index];
  index -= dm_export_count;
  return index < gameplay_export_count ? &gameplay_exports[index] : NULL;
}
static DMStatus api_call(DMContext *c, const char *name, const DMValue *args,
                         uint32_t count, DMValue *out) {
  if (name && !strncmp(name, "native.", 7))
    return gameplay_call(c->host, name, args, count, out);
  return mods_game_call(c->host, name, args, count, out);
}
static DMStatus memory_copy(DMContext *c, uint32_t offset, void *data, size_t n,
                            bool write) {
  wasm_rt_memory_t *m = w2c_game_ib(&c->host->game);
  if (n && !data)
    return DM_EINVAL;
  if ((uint64_t)offset > m->size || n > m->size - offset)
    return DM_EBOUNDS;
  if (n) {
    if (write)
      memcpy(m->data + offset, data, n);
    else
      memcpy(data, m->data + offset, n);
  }
  return DM_OK;
}
static DMStatus api_read(DMContext *c, uint32_t offset, void *data, size_t n) {
  return memory_copy(c, offset, data, n, false);
}
static DMStatus api_write(DMContext *c, uint32_t offset, const void *data,
                          size_t n) {
  return memory_copy(c, offset, (void *)data, n, true);
}
static DMStatus api_allocate(DMContext *c, uint32_t bytes, uint32_t *offset) {
  if (!offset || !bytes || bytes > MAX_BLOB)
    return DM_EINVAL;
  *offset = game_malloc(&c->host->game, bytes);
  return *offset ? DM_OK : DM_EFULL;
}
static DMStatus api_release(DMContext *c, uint32_t offset) {
  // Only free allocations you own: bounds checking cannot prove allocator
  // ownership.
  if ((uint64_t)offset >= w2c_game_ib(&c->host->game)->size)
    return DM_EBOUNDS;
  game_free(&c->host->game, offset);
  return DM_OK;
}
static DMStatus api_setting(DMContext *c, const char *key, char *data,
                            size_t cap, size_t *size) {
  if (!identifier(key) || !size)
    return DM_EINVAL;
  const char *value = get(c->settings, key);
  if (!value)
    return DM_ENOTFOUND;
  *size = strlen(value) + 1;
  if (!data && !cap)
    return DM_OK;
  if (!data)
    return DM_EINVAL;
  if (cap < *size)
    return DM_EBOUNDS;
  memcpy(data, value, *size);
  return DM_OK;
}
static DMStatus api_storage_read(DMContext *c, const char *key, void *data,
                                 size_t cap, size_t *size) {
  if (!identifier(key) || !size)
    return DM_EINVAL;
  int fd = openat(c->data_directory, key,
                  O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
  if (fd < 0)
    return errno == ENOENT ? DM_ENOTFOUND : DM_EIO;
  DMStatus result = read_blob(fd, data, cap, size);
  close(fd);
  return result;
}
static DMStatus api_storage_write(DMContext *c, const char *key,
                                  const void *data, size_t size) {
  if (!identifier(key) || (size && !data) || size > MAX_BLOB)
    return DM_EINVAL;
  char temp[80];
  snprintf(temp, sizeof(temp), ".%s.tmp", key);
  int fd = openat(
      c->data_directory, temp,
      O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
  if (fd < 0)
    return DM_EIO;
  size_t done = 0;
  while (done < size) {
    ssize_t n = write(fd, (const char *)data + done, size - done);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      break;
    done += (size_t)n;
  }
  bool ok = done == size && !fsync(fd);
  if (close(fd))
    ok = false;
  if (ok)
    ok = !renameat(c->data_directory, temp, c->data_directory, key) &&
         !fsync(c->data_directory);
  if (!ok)
    unlinkat(c->data_directory, temp, 0);
  return ok ? DM_OK : DM_EIO;
}
static DMStatus api_storage_remove(DMContext *c, const char *key) {
  if (!identifier(key))
    return DM_EINVAL;
  if (unlinkat(c->data_directory, key, 0))
    return errno == ENOENT ? DM_ENOTFOUND : DM_EIO;
  return fsync(c->data_directory) ? DM_EIO : DM_OK;
}
// Component-wise O_NOFOLLOW traversal: neither asset files nor parent
// directories may be symlinks. Missing overrides fall back; malformed overrides
// fail loudly.
static int override_fd(int directory, const char *asset_path) {
  char copy[4096];
  if (strlen(asset_path) >= sizeof(copy))
    fail("Asset override path too long");
  strcpy(copy, asset_path[0] == '/' ? asset_path + 1 : asset_path);
  int base = dup(directory);
  char *save = NULL, *part = strtok_r(copy, "/", &save);
  while (part) {
    char *next = strtok_r(NULL, "/", &save);
    int fd = openat(base, part,
                    O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK |
                        (next ? O_DIRECTORY : 0));
    int error = errno;
    close(base);
    if (fd < 0) {
      if (error != ENOENT)
        fail("Unsafe or unreadable asset override");
      return -1;
    }
    if (!next)
      return fd;
    base = fd;
    part = next;
  }
  close(base);
  return -1;
}
void mods_asset(struct w2c_a *h, const char *path, const unsigned char **data,
                size_t *size) {
  for (size_t i = 0; i < native_asset_count; i++) {
    const NativeAsset *a = &native_asset_index[i];
    if (strcmp(path, a->path + (path[0] == '/' ? 0 : 1)))
      continue;
    *data = native_assets + a->offset;
    *size = a->size;
    if (!h->mods)
      return;
    Asset *cache = &h->mods->assets[i];
    if (!cache->resolved) {
      cache->data = *data;
      cache->size = *size;
      cache->resolved = true;
      for (uint32_t m = h->mods->count; m > 0; m--) {
        DMContext *c = &h->mods->mods[m - 1];
        if (!c->enabled)
          continue;
        int fd = override_fd(c->directory, path);
        if (fd < 0)
          continue;
        size_t bytes;
        if (read_blob(fd, NULL, 0, &bytes) ||
            h->mods->override_bytes + bytes > 256u * 1024u * 1024u)
          fail("Invalid or excessive asset override");
        unsigned char *replacement = malloc(bytes ? bytes : 1);
        if (!replacement || read_blob(fd, replacement, bytes, &bytes))
          fail("Cannot read asset override");
        close(fd);
        cache->data = replacement;
        cache->size = bytes;
        cache->owned = true;
        h->mods->override_bytes += bytes;
        fprintf(stderr, "[mod:%s] overrides %s (%zu bytes)\n", c->id, path,
                bytes);
        break;
      }
    }
    *data = cache->data;
    *size = cache->size;
    return;
  }
  *data = NULL;
  *size = 0;
}
static uint32_t api_asset_count(DMContext *c) { return native_asset_count; }
static const char *api_asset_path(DMContext *c, uint32_t i) {
  return i < native_asset_count ? native_asset_index[i].path : NULL;
}
static DMStatus api_asset_read(DMContext *c, const char *path, void *data,
                               size_t cap, size_t *size) {
  if (!path || !size)
    return DM_EINVAL;
  const unsigned char *bytes;
  mods_asset(c->host, path, &bytes, size);
  if (!bytes)
    return DM_ENOTFOUND;
  if (!data && !cap)
    return DM_OK;
  if (!data)
    return DM_EINVAL;
  if (cap < *size)
    return DM_EBOUNDS;
  if (*size)
    memcpy(data, bytes, *size);
  return DM_OK;
}
static DMStatus api_publish(DMContext *c, const char *topic, const void *data,
                            size_t size) {
  if (!identifier(topic) || size > 4096 || (size && !data))
    return DM_EINVAL;
  struct ModHost *host = c->host->mods;
  if (host->message_count == 64)
    return DM_EFULL;
  Message *m = &host->messages[host->message_count++];
  strcpy(m->sender, c->id);
  strcpy(m->topic, topic);
  m->size = size;
  if (size)
    memcpy(m->data, data, size);
  return DM_OK;
}
static DMStatus api_title(DMContext *c, const char *text) {
  if (!text || strlen(text) > 255)
    return DM_EINVAL;
  if (!c->host->window)
    return DM_ENOTREADY;
  SDL_SetWindowTitle(c->host->window, text);
  return DM_OK;
}
static const DMApi api = {.abi_version = DM_ABI_VERSION,
                          .struct_size = sizeof(DMApi),
                          .status_string = status_string,
                          .log = api_log,
                          .state = api_state,
                          .export_count = api_export_count,
                          .export_at = api_export_at,
                          .call = api_call,
                          .memory_read = api_read,
                          .memory_write = api_write,
                          .allocate = api_allocate,
                          .release = api_release,
                          .setting = api_setting,
                          .storage_read = api_storage_read,
                          .storage_write = api_storage_write,
                          .storage_remove = api_storage_remove,
                          .asset_count = api_asset_count,
                          .asset_path = api_asset_path,
                          .asset_read = api_asset_read,
                          .publish = api_publish,
                          .window_title = api_title};
const DMApi *mods_api(void) { return &api; }
DMContext *mods_builtin_context(struct w2c_a *h) { return &h->mods->builtin; }

static int storage_root(struct w2c_a *h) {
  int save = open(h->save_dir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (save < 0 || (mkdirat(save, "mods", 0700) && errno != EEXIST))
    fail("Cannot create mod storage");
  int root =
      openat(save, "mods", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  close(save);
  if (root < 0)
    fail("Cannot open mod storage");
  return root;
}

static int compare_names(const void *a, const void *b) {
  return strcmp(*(const char *const *)a, *(const char *const *)b);
}
static bool dependencies_ready(struct ModHost *host, const DMContext *mod,
                               const bool *done) {
  char copy[512];
  strcpy(copy, mod->requires);
  char *save = NULL;
  for (char *part = strtok_r(copy, ",", &save); part;
       part = strtok_r(NULL, ",", &save)) {
    part = trim(part);
    if (!identifier(part))
      fail("Invalid mod dependency ID");
    bool found = false;
    for (uint32_t i = 0; i < host->count; i++)
      if (!strcmp(part, host->mods[i].id) && host->mods[i].enabled) {
        found = true;
        if (!done[i])
          return false;
      }
    if (!found)
      fail("Missing or disabled mod dependency");
  }
  return true;
}
void mods_init(struct w2c_a *h, const char *directory, bool console) {
  h->mods = calloc(1, sizeof(*h->mods));
  if (!h->mods)
    fail("Cannot allocate mod host");
  struct ModHost *host = h->mods;
  host->console = console;
  host->assets = calloc(native_asset_count, sizeof(Asset));
  if (!host->assets)
    fail("Cannot allocate mod asset index");
  DMContext *builtin = &host->builtin;
  builtin->host = h;
  strcpy(builtin->id, "native-menu");
  builtin->directory = builtin->library_fd = -1;
  builtin->settings = calloc(1, sizeof(Ini));
  if (!builtin->settings)
    fail("Cannot allocate menu settings");
  builtin->settings->count = 2;
  strcpy(builtin->settings->entries[0].key, "menu_key");
  strcpy(builtin->settings->entries[0].value, "F2");
  strcpy(builtin->settings->entries[1].key, "profile");
  strcpy(builtin->settings->entries[1].value, "stock-v166");
  int root_storage = storage_root(h);
  if (mkdirat(root_storage, builtin->id, 0700) && errno != EEXIST)
    fail("Cannot create menu storage");
  builtin->data_directory =
      openat(root_storage, builtin->id,
             O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  close(root_storage);
  if (builtin->data_directory < 0)
    fail("Cannot open menu storage");
  if (!directory)
    return; // No automatic arbitrary-code loading.
  int root = open(directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (root < 0)
    fail("Cannot open explicit --mods directory");
  DIR *dir = fdopendir(dup(root));
  if (!dir)
    fail("Cannot scan mods");
  char *names[MAX_MODS];
  uint32_t count = 0;
  struct dirent *entry;
  while ((entry = readdir(dir))) {
    if (entry->d_name[0] == '.')
      continue;
    struct stat st;
    if (fstatat(root, entry->d_name, &st, AT_SYMLINK_NOFOLLOW))
      fail("Cannot stat mod directory");
    if (!S_ISDIR(st.st_mode))
      continue;
    if (count == MAX_MODS)
      fail("At most 64 mod directories are supported");
    names[count++] = strdup(entry->d_name);
  }
  closedir(dir);
  qsort(names, count, sizeof(*names), compare_names);
  for (uint32_t i = 0; i < count; i++) {
    DMContext *c = &host->mods[host->count++];
    c->host = h;
    c->library_fd = -1;
    c->data_directory = -1;
    c->directory =
        openat(root, names[i], O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    free(names[i]);
    if (c->directory < 0)
      fail("Cannot open mod folder");
    Ini *manifest = calloc(1, sizeof(Ini));
    c->settings = calloc(1, sizeof(Ini));
    if (!manifest || !c->settings)
      fail("Cannot allocate mod settings");
    read_ini(c->directory, "mod.ini", manifest, false);
    for (size_t k = 0; k < manifest->count; k++) {
      const char *allowed[] = {"abi",          "id",      "name",
                               "version",      "library", "requires",
                               "game_version", "enabled"};
      bool known = false;
      for (size_t a = 0; a < sizeof(allowed) / sizeof(*allowed); a++)
        if (!strcmp(manifest->entries[k].key, allowed[a]))
          known = true;
      if (!known)
        fail("Unknown mod manifest field");
    }
    const char *abi = get(manifest, "abi");
    if (!abi || strcmp(abi, "1"))
      fail("Mod manifest ABI must be 1");
    copy_field(c->id, sizeof(c->id), get(manifest, "id"), NULL);
    if (!identifier(c->id) || !strcmp(c->id, "native-menu"))
      fail("Invalid mod ID");
    for (uint32_t k = 0; k + 1 < host->count; k++)
      if (!strcmp(c->id, host->mods[k].id))
        fail("Duplicate mod ID");
    copy_field(c->name, sizeof(c->name), get(manifest, "name"), c->id);
    copy_field(c->version, sizeof(c->version), get(manifest, "version"), NULL);
    if (!*c->version)
      fail("Mod version must not be empty");
    copy_field(c->library, sizeof(c->library), get(manifest, "library"), "");
    if (*c->library && (strchr(c->library, '/') || strstr(c->library, "..") ||
                        strlen(c->library) < 4 ||
                        strcmp(c->library + strlen(c->library) - 3, ".so")))
      fail("Mod library must be a .so basename");
    copy_field(c->requires, sizeof(c->requires), get(manifest, "requires"), "");
    const char *enabled = get(manifest, "enabled");
    if (enabled && strcmp(enabled, "true") && strcmp(enabled, "false"))
      fail("enabled must be true or false");
    c->enabled = !enabled || !strcmp(enabled, "true");
    const char *version = get(manifest, "game_version");
    if (version) {
      char *end;
      unsigned long value = strtoul(version, &end, 10);
      if (!*version || *end || value > UINT32_MAX)
        fail("Invalid game_version");
      c->game_version = (uint32_t)value;
    }
    if (c->enabled && c->game_version &&
        c->game_version != game_get_app_version(&h->game))
      fail("Mod game_version does not match compiled game");
    read_ini(c->directory, "settings.ini", c->settings, true);
    free(manifest);
  }
  close(root);
  // Stable topological order. Dependencies precede dependents, also for assets.
  DMContext *ordered = calloc(MAX_MODS, sizeof(DMContext));
  if (!ordered)
    fail("Cannot order mods");
  bool done[MAX_MODS] = {0};
  uint32_t placed = 0;
  while (placed < host->count) {
    bool progress = false;
    for (uint32_t i = 0; i < host->count; i++)
      if (!done[i] && (!host->mods[i].enabled ||
                       dependencies_ready(host, &host->mods[i], done))) {
        ordered[placed++] = host->mods[i];
        done[i] = true;
        progress = true;
        break;
      }
    if (!progress)
      fail("Cyclic mod dependencies");
  }
  memcpy(host->mods, ordered, host->count * sizeof(DMContext));
  free(ordered);
  int mods = storage_root(h);
  for (uint32_t i = 0; i < host->count; i++) {
    DMContext *c = &host->mods[i];
    if (!c->enabled) {
      fprintf(stderr, "[mod:%s] disabled\n", c->id);
      continue;
    }
    if (mkdirat(mods, c->id, 0700) && errno != EEXIST)
      fail("Cannot create per-mod storage");
    c->data_directory =
        openat(mods, c->id, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (c->data_directory < 0)
      fail("Cannot open per-mod storage");
    if (*c->library) {
      c->library_fd = openat(c->directory, c->library,
                             O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
      struct stat st;
      if (c->library_fd < 0 || fstat(c->library_fd, &st) ||
          !S_ISREG(st.st_mode))
        fail("Cannot open mod library");
      char proc[64];
      snprintf(proc, sizeof(proc), "/proc/self/fd/%d", c->library_fd);
      c->library_handle = dlopen(proc, RTLD_NOW | RTLD_LOCAL);
      if (!c->library_handle) {
        fprintf(stderr, "%s\n", dlerror());
        fail("Cannot load mod library");
      }
      DMQuery query = (DMQuery)dlsym(c->library_handle, "dm_mod_query");
      if (!query)
        fail("Mod library must export dm_mod_query");
      c->descriptor = query(DM_ABI_VERSION);
      const DMMod *d = c->descriptor;
      if (!d || d->abi_version != DM_ABI_VERSION ||
          d->struct_size != sizeof(DMMod) || !d->id || !d->name ||
          !d->version || strcmp(d->id, c->id) || strcmp(d->version, c->version))
        fail("Mod descriptor ABI, ID or version does not match manifest");
      // Failure is fatal: do not continue with half-applied plugin side
      // effects.
      if (d->on_load && d->on_load(&api, c, &c->userdata) != DM_OK)
        fail("Mod on_load failed");
    }
    c->loaded = true;
    fprintf(stderr, "[mod:%s] loaded %s (%s)\n", c->id, c->name, c->version);
  }
  close(mods);
}
void mods_tick(struct w2c_a *h, double delta) {
  struct ModHost *host = h->mods;
  if (!host)
    return;
  host->uptime += delta;
  host->frame = (DMFrame){sizeof(DMFrame), h->frames, delta, host->uptime};
  // Snapshot the queue: messages published by callbacks wait until the next
  // tick.
  size_t count = host->message_count;
  Message *messages = count ? malloc(count * sizeof(Message)) : NULL;
  if (count && !messages)
    fail("Cannot dispatch mod messages");
  if (count)
    memcpy(messages, host->messages, count * sizeof(Message));
  host->message_count = 0;
  if (!host->ready && game_get_app_inited(&h->game)) {
    host->ready = true;
    for (uint32_t i = 0; i < host->count; i++) {
      DMContext *c = &host->mods[i];
      if (c->loaded && c->descriptor && c->descriptor->on_ready)
        c->descriptor->on_ready(c->userdata);
    }
  }
  for (size_t m = 0; m < count; m++) {
    menu_message(h, messages[m].sender, messages[m].topic, messages[m].data,
                 messages[m].size);
    for (uint32_t i = 0; i < host->count; i++) {
      DMContext *c = &host->mods[i];
      if (c->loaded && c->descriptor && c->descriptor->on_message)
        c->descriptor->on_message(c->userdata, messages[m].sender,
                                  messages[m].topic, messages[m].data,
                                  messages[m].size);
    }
  }
  free(messages);
  if (host->ready)
    for (uint32_t i = 0; i < host->count; i++) {
      DMContext *c = &host->mods[i];
      if (c->loaded && c->descriptor && c->descriptor->on_frame)
        c->descriptor->on_frame(c->userdata, &host->frame);
    }
}
void mods_render(struct w2c_a *h) {
  if (!mods_ready(h))
    return;
  for (uint32_t i = 0; i < h->mods->count; i++) {
    DMContext *c = &h->mods->mods[i];
    if (c->loaded && c->descriptor && c->descriptor->on_render)
      c->descriptor->on_render(c->userdata, &h->mods->frame);
  }
}
bool mods_input(struct w2c_a *h, const DMInput *event) {
  if (!mods_ready(h))
    return false;
  for (uint32_t i = 0; i < h->mods->count; i++) {
    DMContext *c = &h->mods->mods[i];
    if (c->loaded && c->descriptor && c->descriptor->on_input &&
        c->descriptor->on_input(c->userdata, event))
      return true;
  }
  return false;
}
DMStatus mods_command(struct w2c_a *h, const char *text) {
  if (!h->mods || !text || strlen(text) >= 4096)
    return DM_EINVAL;
  char line[4096];
  strcpy(line, text);
  char *name = trim(line), *args = name;
  while (*args && !isspace((unsigned char)*args))
    args++;
  if (*args)
    *args++ = 0;
  args = trim(args);
  if (!*name)
    return DM_OK;
  if (!strcmp(name, "help")) {
    puts("Native console: help | version | mods | exports [filter] | pause | "
         "resume | reward | menu [open|close] | api <export> [typed numeric "
         "args] | quit\n"
         "  mod <id> <command> [arguments]    Call a plugin command (no shell "
         "evaluation).");
    return DM_OK;
  }
  if (!strcmp(name, "quit")) {
    h->running = false;
    return DM_OK;
  }
  if (!strcmp(name, "version")) {
    printf("Game %u / Native SDK ABI %u\n", game_get_app_version(&h->game),
           DM_ABI_VERSION);
    return DM_OK;
  }
  if (!strcmp(name, "mods")) {
    for (uint32_t i = 0; i < h->mods->count; i++) {
      DMContext *c = &h->mods->mods[i];
      printf("%s %s %s%s\n", c->id, c->version,
             c->enabled ? "enabled" : "disabled",
             *c->library ? " native" : " assets");
      char summary[256];
      snprintf(summary, sizeof(summary), "%s %s %s%s", c->id, c->version,
               c->enabled ? "enabled" : "disabled",
               *c->library ? " native" : " assets");
      menu_log(h, "mods", summary);
    }
    return DM_OK;
  }
  if (!strcmp(name, "exports")) {
    for (uint32_t i = 0; i < api_export_count(&h->mods->builtin); i++) {
      const DMExport *e = api_export_at(&h->mods->builtin, i);
      if (*args && !strstr(e->name, args))
        continue;
      const char *types[] = {"void", "i32", "i64", "f32", "f64"};
      printf("%s(", e->name);
      for (uint32_t a = 0; a < e->argument_count; a++)
        printf("%s%s", a ? ", " : "", types[e->arguments[a]]);
      printf(") -> %s%s\n", types[e->result],
             e->flags & DM_EXPORT_HOST_ONLY ? " [host only]" : "");
    }
    return DM_OK;
  }
  if (!strcmp(name, "menu")) {
    if (!h->menu)
      return DM_ENOTREADY;
    if (*args && strcmp(args, "open") && strcmp(args, "close"))
      return menu_select_tab(h, args);
    menu_set_open(h, strcmp(args, "close") != 0);
    return DM_OK;
  }
  if (!strcmp(name, "api")) {
    char *save = NULL, *target = strtok_r(args, " \t", &save);
    if (!target)
      return DM_EINVAL;
    const DMExport *e = NULL;
    for (u32 i = 0; i < api_export_count(&h->mods->builtin); i++) {
      const DMExport *candidate = api_export_at(&h->mods->builtin, i);
      if (!strcmp(target, candidate->name)) {
        e = candidate;
        break;
      }
    }
    if (!e)
      return DM_ENOTFOUND;
    DMValue values[DM_MAX_ARGS] = {{0}}, result = {0};
    for (u32 i = 0; i < e->argument_count; i++) {
      char *token = strtok_r(NULL, " \t", &save), *end;
      if (!token)
        return DM_EABI;
      values[i].type = e->arguments[i];
      errno = 0;
      switch (values[i].type) {
      case DM_I32: {
        long long v = strtoll(token, &end, 0);
        if (v < INT32_MIN || v > UINT32_MAX)
          return DM_EBOUNDS;
        values[i].as.i32 = (u32)v;
        break;
      }
      case DM_I64:
        values[i].as.i64 = strtoull(token, &end, 0);
        break;
      case DM_F32:
        values[i].as.f32 = strtof(token, &end);
        if (!isfinite(values[i].as.f32))
          return DM_EINVAL;
        break;
      case DM_F64:
        values[i].as.f64 = strtod(token, &end);
        if (!isfinite(values[i].as.f64))
          return DM_EINVAL;
        break;
      default:
        return DM_EABI;
      }
      if (errno || end == token || *end)
        return DM_EINVAL;
    }
    if (strtok_r(NULL, " \t", &save))
      return DM_EABI;
    DMStatus status = api_call(&h->mods->builtin, e->name, values,
                               e->argument_count, &result);
    char message[256];
    snprintf(message, sizeof(message), "%s: %s%s", e->name,
             status_string(status), status ? "" : " (result below)");
    puts(message);
    menu_log(h, "api", message);
    if (!status) {
      switch (result.type) {
      case DM_I32:
        snprintf(message, sizeof(message), "result %u", result.as.i32);
        break;
      case DM_I64:
        snprintf(message, sizeof(message), "result %llu",
                 (unsigned long long)result.as.i64);
        break;
      case DM_F32:
        snprintf(message, sizeof(message), "result %.9g", result.as.f32);
        break;
      case DM_F64:
        snprintf(message, sizeof(message), "result %.17g", result.as.f64);
        break;
      default:
        snprintf(message, sizeof(message), "result void");
      }
      puts(message);
      menu_log(h, "api", message);
    }
    return status;
  }
  if (!strcmp(name, "pause") || !strcmp(name, "resume"))
    return mods_game_call(
        h, !strcmp(name, "pause") ? "app_pause" : "app_resume", NULL, 0, NULL);
  if (!strcmp(name, "reward")) {
    DMValue shown = dm_i32(1);
    DMStatus status =
        mods_game_call(h, "ad_rewarded_on_showed", &shown, 1, NULL);
    return status ? status
                  : mods_game_call(h, "ad_rewarded_on_rewarded", NULL, 0, NULL);
  }
  if (!strcmp(name, "mod")) {
    char *id = args;
    while (*args && !isspace((unsigned char)*args))
      args++;
    if (!*args)
      return DM_EINVAL;
    *args++ = 0;
    args = trim(args);
    char *command = args;
    while (*args && !isspace((unsigned char)*args))
      args++;
    if (*args)
      *args++ = 0;
    args = trim(args);
    if (!*command)
      return DM_EINVAL;
    for (uint32_t i = 0; i < h->mods->count; i++) {
      DMContext *c = &h->mods->mods[i];
      if (!strcmp(id, c->id) && c->loaded && c->descriptor &&
          c->descriptor->on_command)
        return c->descriptor->on_command(c->userdata, command, args);
    }
  }
  return DM_ENOTFOUND;
}
void mods_console(struct w2c_a *h) {
#ifdef _WIN32
  (void)h; // Interactive stdin polling is POSIX-only; use --command or the GUI.
#else
  struct ModHost *m = h->mods;
  if (!m || !m->console)
    return;
  struct pollfd fd = {STDIN_FILENO, POLLIN, 0};
  size_t budget = 4096;
  while (budget && poll(&fd, 1, 0) > 0 && (fd.revents & (POLLIN | POLLHUP))) {
    char bytes[512];
    ssize_t n = read(STDIN_FILENO, bytes, sizeof(bytes));
    if (n <= 0) {
      m->console = false;
      break;
    }
    budget -= (size_t)n;
    for (ssize_t i = 0; i < n; i++) {
      if (bytes[i] == '\n') {
        if (!m->discard_line) {
          m->input[m->input_size] = 0;
          DMStatus s = mods_command(h, m->input);
          if (s)
            fprintf(stderr, "console: %s\n", status_string(s));
        }
        m->input_size = 0;
        m->discard_line = false;
      } else if (!m->discard_line) {
        if (m->input_size + 1 == sizeof(m->input)) {
          m->discard_line = true;
          fprintf(stderr, "console: line too long\n");
        } else
          m->input[m->input_size++] = bytes[i];
      }
    }
    fd.revents = 0;
  }
#endif
}
void mods_unload(struct w2c_a *h) {
  struct ModHost *host = h->mods;
  if (!host)
    return;
  for (uint32_t i = host->count; i > 0; i--) {
    DMContext *c = &host->mods[i - 1];
    if (c->loaded && c->descriptor && c->descriptor->on_unload)
      c->descriptor->on_unload(c->userdata);
    c->loaded = false;
    if (c->library_handle)
      dlclose(c->library_handle);
    c->library_handle = NULL;
    c->descriptor = NULL;
  }
}
void mods_close(struct w2c_a *h) {
  struct ModHost *host = h->mods;
  if (!host)
    return;
  // Called after core shutdown and filesystem_close: open game FDs may still
  // reference override buffers until then. Callbacks were already unloaded.
  for (uint32_t i = host->count; i > 0; i--) {
    DMContext *c = &host->mods[i - 1];
    if (c->library_fd >= 0)
      close(c->library_fd);
    if (c->data_directory >= 0)
      close(c->data_directory);
    close(c->directory);
    free(c->settings);
  }
  for (size_t i = 0; i < native_asset_count; i++)
    if (host->assets[i].owned)
      free((void *)host->assets[i].data);
  close(host->builtin.data_directory);
  free(host->builtin.settings);
  free(host->assets);
  free(host);
  h->mods = NULL;
}
