// Time Warp: cycle the game clock with F9, or use `mod time-warp set 0.35`.
#include <drive_mad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const DMApi *api;
  DMContext *ctx;
  unsigned index;
} Warp;

static const float speeds[] = {0.25f, 0.5f, 1.0f, 1.5f, 2.5f};
static float distance(float a, float b) { return a > b ? a - b : b - a; }
static DMStatus apply(Warp *w) {
  DMValue value = dm_f32(speeds[w->index]);
  DMStatus s = w->api->call(w->ctx, "native.time_scale", &value, 1, NULL);
  char title[96];
  snprintf(title, sizeof(title), "Drive Mad - Time Warp %.0f%%",
           speeds[w->index] * 100);
  if (!s)
    w->api->window_title(w->ctx, title);
  return s;
}
static DMStatus load(const DMApi *api, DMContext *ctx, void **userdata) {
  if (api->abi_version != DM_ABI_VERSION || api->struct_size < sizeof(*api))
    return DM_EABI;
  Warp *w = calloc(1, sizeof(*w));
  if (!w)
    return DM_EFULL;
  w->api = api;
  w->ctx = ctx;
  w->index = 2;
  *userdata = w;
  api->log(ctx, "Time Warp loaded. F9 cycles speed; command: set <0.05..3>.");
  return DM_OK;
}
static void ready(void *userdata) {
  Warp *w = userdata;
  DMValue current = {};
  if (w->api->call(w->ctx, "native.get_time_scale", NULL, 0, &current) ==
          DM_OK &&
      current.as.f32 >= 0.05f && current.as.f32 <= 3.0f) {
    unsigned best = 0;
    for (unsigned i = 1; i < 5; i++)
      if (distance(current.as.f32, speeds[i]) <
          distance(current.as.f32, speeds[best]))
        best = i;
    w->index = best;
  }
  apply(w);
}
static int input(void *userdata, const DMInput *event) {
  Warp *w = userdata;
  if (event->type != DM_INPUT_KEY || event->key != 298)
    return 0;
  if (event->action == 1) {
    w->index = (w->index + 1) % 5;
    if (apply(w))
      w->api->log(w->ctx, "Time Warp: clock change rejected");
  }
  return 1;
}
static DMStatus command(void *userdata, const char *name,
                        const char *arguments) {
  Warp *w = userdata;
  if (!strcmp(name, "cycle") || !strcmp(name, "next")) {
    w->index = (w->index + 1) % 5;
    return apply(w);
  }
  if (!strcmp(name, "reset")) {
    w->index = 2;
    return apply(w);
  }
  if (!strcmp(name, "info")) {
    char text[96];
    snprintf(text, sizeof(text), "speed=%.2fx", speeds[w->index]);
    w->api->log(w->ctx, text);
    return DM_OK;
  }
  if (!strcmp(name, "set")) {
    char *end = NULL;
    double value = arguments ? strtod(arguments, &end) : 0;
    if (!arguments || !*arguments || (end && *end) || value < 0.05 ||
        value > 3.0)
      return DM_EINVAL;
    DMValue speed = dm_f32((float)value);
    DMStatus s = w->api->call(w->ctx, "native.time_scale", &speed, 1, NULL);
    if (!s)
      w->api->window_title(w->ctx, "Drive Mad - Time Warp (custom)");
    return s;
  }
  return DM_ENOTFOUND;
}
static void unload(void *userdata) {
  Warp *w = userdata;
  DMValue one = dm_f32(1);
  w->api->call(w->ctx, "native.time_scale", &one, 1, NULL);
  w->api->log(w->ctx, "Time Warp unloaded; clock restored to 1x");
  free(w);
}
DM_PUBLIC const DMMod *dm_mod_query(uint32_t abi) {
  static const DMMod mod = {.abi_version = DM_ABI_VERSION,
                            .struct_size = sizeof(DMMod),
                            .id = "time-warp",
                            .name = "Time Warp",
                            .version = "1.0.0",
                            .on_load = load,
                            .on_ready = ready,
                            .on_input = input,
                            .on_command = command,
                            .on_unload = unload};
  return abi == DM_ABI_VERSION ? &mod : NULL;
}
