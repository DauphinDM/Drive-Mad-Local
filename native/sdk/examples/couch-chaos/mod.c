// Couch Chaos: a genuinely local, turn-based two-player mode for the native
// build. It is deliberate hot-seat play, not a revival of the original online
// Versus service or a split-screen renderer.
#include <drive_mad.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const DMApi *api;
  DMContext *ctx;
  uint32_t wins[2];
  unsigned player;
  bool running, supported;
  double started, last_title;
  char prefix[64];
} Chaos;

static double uptime(Chaos *c) {
  DMState state = {.struct_size = sizeof(state)};
  return c->api->state(c->ctx, &state) == DM_OK ? state.uptime_seconds : 0;
}
static void persist(Chaos *c) {
  DMStatus s = c->api->storage_write(c->ctx, "wins", c->wins, sizeof(c->wins));
  if (s)
    c->api->log(c->ctx, "Could not persist Couch Chaos wins");
}
static void set_title(Chaos *c, bool force) {
  double now = uptime(c);
  if (!force && now - c->last_title < 0.25)
    return;
  c->last_title = now;
  char title[256];
  if (c->running) {
    unsigned seconds = (unsigned)(now - c->started);
    snprintf(title, sizeof(title),
             "%s | P%u %u:%02u | wins %u-%u | F7 restart  F8 switch  F9 win",
             c->prefix, c->player + 1, seconds / 60, seconds % 60, c->wins[0],
             c->wins[1]);
  } else {
    snprintf(title, sizeof(title),
             "%s | P%u to play | wins %u-%u | F7 start  F8 switch  F9 win",
             c->prefix, c->player + 1, c->wins[0], c->wins[1]);
  }
  c->api->window_title(c->ctx, title);
}
static DMStatus restart(Chaos *c, bool switch_player) {
  if (!c->supported)
    return DM_EPROTECTED;
  if (switch_player)
    c->player ^= 1;
  DMStatus s = dm_call0(c->api, c->ctx, "native.restart");
  if (!s) {
    c->running = true;
    c->started = uptime(c);
    char text[96];
    snprintf(text, sizeof(text), "Player %u run started on the same level.",
             c->player + 1);
    c->api->log(c->ctx, text);
  }
  set_title(c, true);
  return s;
}
static DMStatus claim_win(Chaos *c) {
  if (!c->running)
    return DM_EINVAL;
  c->wins[c->player]++;
  c->running = false;
  persist(c);
  char text[128];
  snprintf(text, sizeof(text), "Player %u wins. Score %u-%u.", c->player + 1,
           c->wins[0], c->wins[1]);
  c->api->log(c->ctx, text);
  c->player ^= 1;
  set_title(c, true);
  return DM_OK;
}
static DMStatus load(const DMApi *api, DMContext *ctx, void **userdata) {
  if (api->abi_version != DM_ABI_VERSION || api->struct_size < sizeof(*api))
    return DM_EABI;
  Chaos *c = calloc(1, sizeof(*c));
  if (!c)
    return DM_EFULL;
  c->api = api;
  c->ctx = ctx;
  strcpy(c->prefix, "Couch Chaos");
  size_t size = 0;
  if (api->storage_read(ctx, "wins", c->wins, sizeof(c->wins), &size) !=
          DM_OK ||
      size != sizeof(c->wins))
    c->wins[0] = c->wins[1] = 0;
  DMValue profile = {};
  c->supported =
      api->call(ctx, "native.profile_supported", NULL, 0, &profile) == DM_OK &&
      profile.as.i32 != 0;
  *userdata = c;
  api->log(ctx, "Couch Chaos loaded: local hot-seat, not online Versus.");
  return DM_OK;
}
static void ready(void *userdata) {
  Chaos *c = userdata;
  size_t size = 0;
  DMValue profile = {};
  c->supported = c->api->call(c->ctx, "native.profile_supported", NULL, 0,
                              &profile) == DM_OK &&
                 profile.as.i32 != 0;
  if (c->api->setting(c->ctx, "title_prefix", c->prefix, sizeof(c->prefix),
                      &size) != DM_OK)
    strcpy(c->prefix, "Couch Chaos");
  c->api->log(c->ctx,
              c->supported
                  ? "Couch Chaos ready: F7 start/restart, F8 hand over, "
                    "F9 claim the win."
                  : "Couch Chaos: the stock physics profile is "
                    "unavailable; level restart is disabled.");
  set_title(c, true);
}
static void frame(void *userdata, const DMFrame *event) {
  (void)event;
  Chaos *c = userdata;
  if (c->running)
    set_title(c, false);
}
static int input(void *userdata, const DMInput *event) {
  Chaos *c = userdata;
  if (event->type != DM_INPUT_KEY || event->key < 296 || event->key > 298)
    return 0;
  if (event->action == 1) {
    if (event->key == 296)
      restart(c, false);
    else if (event->key == 297)
      restart(c, true);
    else
      claim_win(c);
  }
  return 1;
}
static DMStatus command(void *userdata, const char *name,
                        const char *arguments) {
  (void)arguments;
  Chaos *c = userdata;
  if (!strcmp(name, "restart"))
    return restart(c, false);
  if (!strcmp(name, "switch"))
    return restart(c, true);
  if (!strcmp(name, "win"))
    return claim_win(c);
  if (!strcmp(name, "reset")) {
    c->wins[0] = c->wins[1] = 0;
    c->player = 0;
    c->running = false;
    persist(c);
    set_title(c, true);
    return DM_OK;
  }
  if (!strcmp(name, "info")) {
    char text[256];
    snprintf(text, sizeof(text), "player=%u running=%u wins=%u-%u supported=%u",
             c->player + 1, c->running, c->wins[0], c->wins[1], c->supported);
    c->api->log(c->ctx, text);
    return DM_OK;
  }
  return DM_ENOTFOUND;
}
static void unload(void *userdata) {
  Chaos *c = userdata;
  c->api->log(c->ctx, "Couch Chaos unloaded");
  free(c);
}
DM_PUBLIC const DMMod *dm_mod_query(uint32_t abi) {
  static const DMMod mod = {.abi_version = DM_ABI_VERSION,
                            .struct_size = sizeof(DMMod),
                            .id = "couch-chaos",
                            .name = "Couch Chaos",
                            .version = "1.0.0",
                            .on_load = load,
                            .on_ready = ready,
                            .on_frame = frame,
                            .on_input = input,
                            .on_command = command,
                            .on_unload = unload};
  return abi == DM_ABI_VERSION ? &mod : NULL;
}
