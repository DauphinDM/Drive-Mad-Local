// Small toy-box mod for the checked native gameplay API. Every operation acts
// on the whole simulated world; no player-car identity is assumed.
#include <drive_mad.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const DMApi *api;
  DMContext *ctx;
  unsigned phase;
  bool supported;
} Fun;

static DMValue query(Fun *f, const char *name) {
  DMValue value = {};
  if (f->api->call(f->ctx, name, NULL, 0, &value))
    value = (DMValue){};
  return value;
}
static DMStatus gravity(Fun *f, float x, float y, float z) {
  DMValue args[3] = {dm_f32(x), dm_f32(y), dm_f32(z)};
  DMStatus s = f->api->call(f->ctx, "native.gravity", args, 3, NULL);
  if (s) {
    f->api->log(f->ctx, "Fun Pack: gravity rejected (profile or parameters)");
  } else {
    char text[96];
    snprintf(text, sizeof(text), "Fun Pack: gravity %.2f %.2f %.2f", x, y, z);
    f->api->log(f->ctx, text);
  }
  return s;
}
static DMStatus material(Fun *f, float friction, float bounce) {
  if (!f->supported)
    return DM_EPROTECTED;
  uint32_t bodies = query(f, "native.body_count").as.i32, changed = 0;
  for (uint32_t i = 0; i < bodies && i < 4096; i++) {
    DMValue args[2] = {dm_i32(i), dm_f32(friction)};
    if (!f->api->call(f->ctx, "native.body_friction", args, 2, NULL))
      changed++;
    args[1] = dm_f32(bounce);
    f->api->call(f->ctx, "native.body_bounce", args, 2, NULL);
  }
  char text[128];
  snprintf(text, sizeof(text), "Fun Pack: material preset on %u bodies.",
           changed);
  f->api->log(f->ctx, text);
  return DM_OK;
}
static DMStatus roulette(Fun *f) {
  static const float table[][3] = {{0, -1.62f, 0}, {0, 0, 0},   {8, -9.8f, 0},
                                   {0, 9.8f, 0},   {-7, -6, 0}, {0, -24, 0}};
  const float *v = table[f->phase++ % 6];
  return gravity(f, v[0], v[1], v[2]);
}
static DMStatus action(Fun *f, const char *name) {
  if (!strcmp(name, "moon"))
    return gravity(f, 0, -1.62f, 0);
  if (!strcmp(name, "zero"))
    return gravity(f, 0, 0, 0);
  if (!strcmp(name, "heavy"))
    return gravity(f, 0, -24, 0);
  if (!strcmp(name, "sideways"))
    return gravity(f, 9, -9.8f, 0);
  if (!strcmp(name, "roulette"))
    return roulette(f);
  if (!strcmp(name, "bouncy"))
    return material(f, 0.15f, 0.95f);
  if (!strcmp(name, "ice"))
    return material(f, 0, 0.05f);
  if (!strcmp(name, "sticky"))
    return material(f, 2.5f, 0);
  if (!strcmp(name, "launch")) {
    DMValue lift = dm_f32(14);
    return f->api->call(f->ctx, "native.world_lift", &lift, 1, NULL);
  }
  if (!strcmp(name, "freeze"))
    return dm_call0(f->api, f->ctx, "native.stop_motion");
  if (!strcmp(name, "reset")) {
    DMValue one = dm_f32(1);
    f->api->call(f->ctx, "native.gravity_reset", NULL, 0, NULL);
    return f->api->call(f->ctx, "native.time_scale", &one, 1, NULL);
  }
  return DM_ENOTFOUND;
}
static DMStatus load(const DMApi *api, DMContext *ctx, void **userdata) {
  if (api->abi_version != DM_ABI_VERSION || api->struct_size < sizeof(*api))
    return DM_EABI;
  Fun *f = calloc(1, sizeof(*f));
  if (!f)
    return DM_EFULL;
  f->api = api;
  f->ctx = ctx;
  *userdata = f;
  api->log(ctx, "Fun Pack loaded. F7 gravity roulette, F8 bouncy world, F9 "
                "ice rink, F10 freeze.");
  return DM_OK;
}
static void ready(void *userdata) {
  Fun *f = userdata;
  f->supported = query(f, "native.profile_supported").as.i32 != 0;
  f->api->window_title(f->ctx, "Drive Mad - Fun Pack");
  if (!f->supported)
    f->api->log(f->ctx, "Fun Pack: stock physics profile unavailable; toy "
                        "actions will fail closed.");
}
static int input(void *userdata, const DMInput *event) {
  Fun *f = userdata;
  if (event->type != DM_INPUT_KEY || event->key < 296 || event->key > 299)
    return 0;
  if (event->action == 1) {
    if (event->key == 296)
      roulette(f);
    else if (event->key == 297)
      material(f, 0.15f, 0.95f);
    else if (event->key == 298)
      material(f, 0, 0.05f);
    else
      dm_call0(f->api, f->ctx, "native.stop_motion");
  }
  return 1;
}
static DMStatus command(void *userdata, const char *name,
                        const char *arguments) {
  (void)arguments;
  Fun *f = userdata;
  if (!strcmp(name, "info")) {
    char text[160];
    snprintf(text, sizeof(text), "supported=%u phase=%u", f->supported,
             f->phase);
    f->api->log(f->ctx, text);
    return DM_OK;
  }
  return action(f, name);
}
static void unload(void *userdata) {
  Fun *f = userdata;
  DMValue one = dm_f32(1);
  f->api->call(f->ctx, "native.gravity_reset", NULL, 0, NULL);
  f->api->call(f->ctx, "native.time_scale", &one, 1, NULL);
  f->api->log(f->ctx, "Fun Pack unloaded; gravity and time scale restored.");
  free(f);
}
DM_PUBLIC const DMMod *dm_mod_query(uint32_t abi) {
  static const DMMod mod = {.abi_version = DM_ABI_VERSION,
                            .struct_size = sizeof(DMMod),
                            .id = "fun-pack",
                            .name = "Fun Pack",
                            .version = "1.0.0",
                            .on_load = load,
                            .on_ready = ready,
                            .on_input = input,
                            .on_command = command,
                            .on_unload = unload};
  return abi == DM_ABI_VERSION ? &mod : NULL;
}
