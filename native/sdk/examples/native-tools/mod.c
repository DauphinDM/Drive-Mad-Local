#include <drive_mad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const DMApi *api;
  DMContext *ctx;
  unsigned launches;
} Tools;

static DMStatus load(const DMApi *api, DMContext *ctx, void **userdata) {
  if (api->abi_version != DM_ABI_VERSION || api->struct_size < sizeof(*api))
    return DM_EABI;
  Tools *tools = calloc(1, sizeof(*tools));
  if (!tools)
    return DM_EFULL;
  tools->api = api;
  tools->ctx = ctx;
  *userdata = tools;
  size_t size;
  DMStatus result = api->storage_read(ctx, "launches", &tools->launches,
                                      sizeof(tools->launches), &size);
  if (result != DM_OK || size != sizeof(tools->launches))
    tools->launches = 0;
  tools->launches++;
  result = api->storage_write(ctx, "launches", &tools->launches,
                              sizeof(tools->launches));
  if (result != DM_OK) {
    free(tools);
    *userdata = NULL;
    return result;
  }
  api->log(ctx,
           "Native Tools loaded. F6: reward. Console: mod native-tools info");
  return DM_OK;
}
static void ready(void *userdata) {
  Tools *t = userdata;
  char value[256];
  size_t size;
  if (t->api->setting(t->ctx, "title", value, sizeof(value), &size) == DM_OK)
    t->api->window_title(t->ctx, value);
  if (t->api->setting(t->ctx, "ad_frequency", value, sizeof(value), &size) ==
      DM_OK) {
    char *end;
    unsigned long n = strtoul(value, &end, 10);
    if (*value && !*end && n <= UINT32_MAX) {
      DMValue arg = dm_i32((uint32_t)n);
      t->api->call(t->ctx, "set_ad_freq", &arg, 1, NULL);
    }
  }
  t->api->log(t->ctx, "ready");
}
static DMStatus reward(Tools *t) {
  DMValue shown = dm_i32(1);
  DMStatus s = t->api->call(t->ctx, "ad_rewarded_on_showed", &shown, 1, NULL);
  return s ? s : dm_call0(t->api, t->ctx, "ad_rewarded_on_rewarded");
}
static int input(void *userdata, const DMInput *event) {
  // GLFW F6=295. Consume both press and release, not just the press.
  if (event->type != DM_INPUT_KEY || event->key != 295)
    return 0;
  if (event->action == 1)
    reward(userdata);
  return 1;
}
static DMStatus command(void *userdata, const char *name,
                        const char *arguments) {
  Tools *t = userdata;
  if (!strcmp(name, "reward"))
    return reward(t);
  if (!strcmp(name, "pause"))
    return dm_call0(t->api, t->ctx, "app_pause");
  if (!strcmp(name, "resume"))
    return dm_call0(t->api, t->ctx, "app_resume");
  if (!strcmp(name, "info")) {
    DMState state = {.struct_size = sizeof(state)};
    DMStatus s = t->api->state(t->ctx, &state);
    if (s)
      return s;
    char text[256];
    snprintf(text, sizeof(text),
             "game=%u ready=%u frames=%llu launches=%u exports=%u",
             state.game_version, state.ready, (unsigned long long)state.frames,
             t->launches, t->api->export_count(t->ctx));
    t->api->log(t->ctx, text);
    return DM_OK;
  }
  return DM_ENOTFOUND;
}
static void unload(void *userdata) {
  Tools *t = userdata;
  t->api->log(t->ctx, "unloaded");
  free(t);
}
DM_PUBLIC const DMMod *dm_mod_query(uint32_t abi) {
  static const DMMod mod = {.abi_version = DM_ABI_VERSION,
                            .struct_size = sizeof(DMMod),
                            .id = "native-tools",
                            .name = "Native Tools",
                            .version = "1.0.0",
                            .on_load = load,
                            .on_ready = ready,
                            .on_input = input,
                            .on_command = command,
                            .on_unload = unload};
  return abi == DM_ABI_VERSION ? &mod : NULL;
}
