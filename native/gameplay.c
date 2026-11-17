// Stock-build profile recovered from f667/f131/f233/f435/f797/f281/f387.
// See sdk/GAMEPLAY-RESEARCH.md. Never apply these offsets to an unknown build.
#include "gameplay-profile.h"
#include "host.h"
#include <math.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include "win32_compat.h"
#endif

#define ENTRY(n, count, result, ...)                                           \
  { "native." n, 0, count, {__VA_ARGS__}, result }
#define ENTRY_EARLY(n, count, result, ...)                                     \
  { "native." n, DM_EXPORT_EARLY, count, {__VA_ARGS__}, result }
const DMExport gameplay_exports[] = {
    ENTRY_EARLY("profile_supported", 0, DM_I32, DM_VOID),
    ENTRY("time_scale", 1, DM_VOID, DM_F32),
    ENTRY("get_time_scale", 0, DM_F32, DM_VOID),
    ENTRY("pause", 1, DM_VOID, DM_I32),
    ENTRY("step", 0, DM_VOID, DM_VOID),
    ENTRY("auto_drive", 1, DM_VOID, DM_I32),
    ENTRY("get_auto_drive", 0, DM_I32, DM_VOID),
    ENTRY("invert_controls", 1, DM_VOID, DM_I32),
    ENTRY("get_invert_controls", 0, DM_I32, DM_VOID),
    ENTRY("level_count", 0, DM_I32, DM_VOID),
    ENTRY("level_index", 0, DM_I32, DM_VOID),
    ENTRY("start_level", 1, DM_VOID, DM_I32),
    ENTRY("restart", 0, DM_VOID, DM_VOID),
    ENTRY("next_level", 0, DM_VOID, DM_VOID),
    ENTRY("gravity", 3, DM_VOID, DM_F32, DM_F32, DM_F32),
    ENTRY("gravity_reset", 0, DM_VOID, DM_VOID),
    ENTRY("gravity_x", 0, DM_F32, DM_VOID),
    ENTRY("gravity_y", 0, DM_F32, DM_VOID),
    ENTRY("gravity_z", 0, DM_F32, DM_VOID),
    ENTRY("body_count", 0, DM_I32, DM_VOID),
    ENTRY("body_velocity", 4, DM_VOID, DM_I32, DM_F32, DM_F32, DM_F32),
    ENTRY("body_spin", 4, DM_VOID, DM_I32, DM_F32, DM_F32, DM_F32),
    ENTRY("body_force", 4, DM_VOID, DM_I32, DM_F32, DM_F32, DM_F32),
    ENTRY("body_friction", 2, DM_VOID, DM_I32, DM_F32),
    ENTRY("body_bounce", 2, DM_VOID, DM_I32, DM_F32),
    ENTRY("body_x", 1, DM_F32, DM_I32),
    ENTRY("body_y", 1, DM_F32, DM_I32),
    ENTRY("body_z", 1, DM_F32, DM_I32),
    ENTRY("body_vx", 1, DM_F32, DM_I32),
    ENTRY("body_vy", 1, DM_F32, DM_I32),
    ENTRY("body_vz", 1, DM_F32, DM_I32),
    ENTRY("body_wx", 1, DM_F32, DM_I32),
    ENTRY("body_wy", 1, DM_F32, DM_I32),
    ENTRY("body_wz", 1, DM_F32, DM_I32),
    ENTRY("body_friction_value", 1, DM_F32, DM_I32),
    ENTRY("body_bounce_value", 1, DM_F32, DM_I32),
    ENTRY("body_inverse_mass", 1, DM_F32, DM_I32),
    ENTRY("stop_motion", 0, DM_VOID, DM_VOID),
    ENTRY("world_lift", 1, DM_VOID, DM_F32),
    ENTRY("reset_controls", 0, DM_VOID, DM_VOID),
};
const uint32_t gameplay_export_count =
    sizeof(gameplay_exports) / sizeof(*gameplay_exports);

static bool range(struct w2c_a *h, u32 p, size_t n) {
  wasm_rt_memory_t *m = w2c_game_ib(&h->game);
  return p && p <= m->size && n <= m->size - p;
}
static float get_float(struct w2c_a *h, u32 p) {
  float f;
  memcpy(&f, mem(h, p, 4), 4);
  return f;
}
static void put_float(struct w2c_a *h, u32 p, float f) {
  memcpy(mem(h, p, 4), &f, 4);
}
static void wake(struct w2c_a *h, u32 body) {
  // Exactly the activation-state logic used by f233(body, 1).
  if ((read32(h, body + 216) & ~2u) != 4)
    write32(h, body + 216, 1);
  write32(h, body + 220, 0);
}
static DMStatus body_at(struct w2c_a *h, u32 index, u32 *body) {
  u32 count = read32(h, 26417664), table = read32(h, 32434048);
  if (count > 16384 || index >= count)
    return DM_EBOUNDS;
  if (!range(h, table, (size_t)count * 4))
    return DM_ENOTREADY;
  *body = read32(h, table + index * 4);
  return range(h, *body, 640) ? DM_OK : DM_ENOTREADY;
}
static DMStatus gravity(struct w2c_a *h, float values[3], bool set) {
  u32 world = read32(h, 32434056);
  if (!range(h, world, 4))
    return DM_ENOTREADY;
  u32 vtable = read32(h, world);
  if (!range(h, vtable, 80))
    return DM_EABI;
  u32 index = read32(h, vtable + (set ? 18 : 19) * 4);
  wasm_rt_funcref_table_t *table = w2c_game_kb(&h->game);
  if (index >= table->size)
    return DM_EABI;
  wasm_rt_funcref_t ref = table->data[index];
  wasm_rt_func_type_t type =
      wasm2c_game_get_func_type(2, 0, WASM_RT_I32, WASM_RT_I32);
  if (!ref.func || !ref.func_type || memcmp(ref.func_type, type, 32))
    return DM_EABI;
  if (!h->gameplay.scratch)
    h->gameplay.scratch = game_malloc(&h->game, 16);
  u32 p = h->gameplay.scratch;
  if (!range(h, p, 16))
    return DM_EFULL;
  typedef void (*Call)(void *, u32, u32);
  if (set) {
    memcpy(mem(h, p, 12), values, 12);
    write32(h, p + 12, 0);
    ((Call)ref.func)(ref.module_instance, world, p);
    u32 count = read32(h, 26417664);
    if (count > 16384)
      return DM_EBOUNDS;
    for (u32 i = 0, b; i < count; i++)
      if (body_at(h, i, &b) == DM_OK)
        wake(h, b);
  } else {
    ((Call)ref.func)(ref.module_instance, p, world);
    memcpy(values, mem(h, p, 12), 12);
  }
  return DM_OK;
}
void gameplay_init(struct w2c_a *h) { h->gameplay.speed = 1; }
double gameplay_clock(struct w2c_a *h) {
  if (!h->gameplay.clock_started) {
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    h->gameplay.clock_ms = t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
    h->gameplay.clock_real = SDL_GetTicks64();
    h->gameplay.clock_started = true;
  }
  return h->gameplay.clock_ms;
}
int gameplay_key(struct w2c_a *h, int code) {
  if (!h->gameplay.invert)
    return code;
  switch (code) {
  case 65:
    return 68;
  case 68:
    return 65;
  case 262:
    return 263;
  case 263:
    return 262;
  default:
    return code;
  }
}
void gameplay_tick(struct w2c_a *h) {
  gameplay_clock(h);
  uint64_t now = SDL_GetTicks64(), elapsed = now - h->gameplay.clock_real;
  h->gameplay.clock_real = now;
  if (!h->paused)
    h->gameplay.clock_ms += (double)elapsed * h->gameplay.speed;
  else if (h->gameplay.steps)
    h->gameplay.clock_ms += 1000.0 / 60.0;
  if (!mods_ready(h))
    return;
  platform_drive(h);
  if (DM_GAMEPLAY_PROFILE && h->gameplay.gravity_enabled) {
    float g[3];
    memcpy(g, h->gameplay.gravity, sizeof(g));
    gravity(h, g, true);
  }
}
void gameplay_pause(struct w2c_a *h, bool paused) {
  // The recovered app exports pause the core/audio but do not call
  // emscripten_pause_main_loop. Keep the host scheduler/state in sync.
  if (paused)
    game_app_pause(&h->game);
  else
    game_app_resume(&h->game);
  h->paused = paused;
}
void gameplay_after_frame(struct w2c_a *h, bool stepped) {
  if (stepped) {
    if (h->gameplay.steps)
      h->gameplay.steps--;
    gameplay_pause(h, true);
  }
}
DMStatus gameplay_call(struct w2c_a *h, const char *name, const DMValue *args,
                       uint32_t count, DMValue *out) {
  if (!name || (count && !args))
    return DM_EINVAL;
  const DMExport *entry = NULL;
  for (u32 i = 0; i < gameplay_export_count; i++)
    if (!strcmp(name, gameplay_exports[i].name)) {
      entry = &gameplay_exports[i];
      break;
    }
  if (!entry)
    return DM_ENOTFOUND;
  if (count != entry->argument_count)
    return DM_EABI;
  for (u32 i = 0; i < count; i++) {
    if (args[i].type != entry->arguments[i])
      return DM_EABI;
    if (args[i].type == DM_F32 && !isfinite(args[i].as.f32))
      return DM_EINVAL;
  }
  DMValue ignored = {0};
  if (!out)
    out = &ignored;
  memset(out, 0, sizeof(*out));
  out->type = entry->result;
  const char *n = name + 7;
  if (!strcmp(n, "profile_supported")) {
    out->as.i32 = DM_GAMEPLAY_PROFILE;
    return DM_OK;
  }
  if (!mods_ready(h))
    return DM_ENOTREADY;
  Gameplay *g = &h->gameplay;
  if (!strcmp(n, "get_time_scale")) {
    out->as.f32 = g->speed;
    return DM_OK;
  }
  if (!strcmp(n, "time_scale")) {
    if (args[0].as.f32 < 0.05f || args[0].as.f32 > 3)
      return DM_EBOUNDS;
    g->speed = args[0].as.f32;
    return DM_OK;
  }
  if (!strcmp(n, "pause")) {
    if (args[0].as.i32 > 1)
      return DM_EINVAL;
    if (args[0].as.i32)
      gameplay_pause(h, true);
    else {
      g->steps = 0;
      gameplay_pause(h, false);
    }
    return DM_OK;
  }
  if (!strcmp(n, "step")) {
    if (!h->paused)
      return DM_EINVAL;
    if (g->steps >= 60)
      return DM_EFULL;
    g->steps++;
    return DM_OK;
  }
  if (!strcmp(n, "get_auto_drive")) {
    out->as.i32 = (u32)g->auto_drive;
    return DM_OK;
  }
  if (!strcmp(n, "get_invert_controls")) {
    out->as.i32 = g->invert;
    return DM_OK;
  }
  if (!strcmp(n, "auto_drive")) {
    if ((s32)args[0].as.i32 < -1 || (s32)args[0].as.i32 > 1)
      return DM_EBOUNDS;
    g->auto_drive = (s32)args[0].as.i32;
    platform_drive(h);
    return DM_OK;
  }
  if (!strcmp(n, "invert_controls")) {
    if (args[0].as.i32 > 1)
      return DM_EINVAL;
    platform_release_keys(h);
    g->invert = args[0].as.i32;
    return DM_OK;
  }
  if (!strcmp(n, "reset_controls")) {
    g->speed = 1;
    g->steps = 0;
    g->auto_drive = 0;
    g->invert = false;
    platform_release_keys(h);
    return DM_OK;
  }
  // No undocumented-layout access on a modified or unknown game binary.
  if (!DM_GAMEPLAY_PROFILE)
    return DM_EPROTECTED;
  if (!strcmp(n, "level_count")) {
    out->as.i32 = read32(h, 10320272);
    return DM_OK;
  }
  if (!strcmp(n, "level_index")) {
    out->as.i32 = read32(h, 21361236);
    return DM_OK;
  }
  if (!strcmp(n, "start_level") || !strcmp(n, "restart") ||
      !strcmp(n, "next_level")) {
    u32 levels = read32(h, 10320272);
    u32 level =
        !strcmp(n, "start_level") ? args[0].as.i32 : read32(h, 21361236);
    if (!strcmp(n, "next_level"))
      level++;
    if (!levels || levels > 10000 || level >= levels)
      return DM_EBOUNDS;
    // Keep reloads outside reentrant sequences in a single completed frame.
    if (g->level_changed && g->level_frame == h->frames)
      return DM_EFULL;
    g->level_changed = true;
    g->level_frame = h->frames;
#if DM_GAMEPLAY_PROFILE
    // The web export assumes the level-selection UI already tore down physics.
    // f288 pairs f435's camera store and destroys the old rigid-body world;
    // skipping it leaks worlds and overflows camera backups on later reloads.
    game_physics_cleanup(&h->game);
#endif
    game_level_select_menu_start_level(&h->game, level);
    return DM_OK;
  }
  if (!strcmp(n, "body_count")) {
    out->as.i32 = read32(h, 26417664);
    return DM_OK;
  }
  if (!strcmp(n, "gravity")) {
    for (u32 i = 0; i < 3; i++)
      if (fabsf(args[i].as.f32) > 100)
        return DM_EBOUNDS;
    float old[3];
    DMStatus status = gravity(h, old, false);
    if (status != DM_OK)
      return status;
    // Capture the actual current game vector, not an assumed Earth preset.
    if (!g->gravity_enabled)
      memcpy(g->gravity_original, old, sizeof(old));
    for (u32 i = 0; i < 3; i++)
      g->gravity[i] = args[i].as.f32;
    status = gravity(h, g->gravity, true);
    if (status == DM_OK)
      g->gravity_enabled = true;
    return status;
  }
  if (!strcmp(n, "gravity_reset")) {
    if (!g->gravity_enabled)
      return DM_OK;
    DMStatus status = gravity(h, g->gravity_original, true);
    if (status == DM_OK)
      g->gravity_enabled = false;
    return status;
  }
  if (!strncmp(n, "gravity_", 8) && strlen(n) == 9) {
    float v[3];
    DMStatus status = gravity(h, v, false);
    if (status == DM_OK)
      out->as.f32 = v[n[8] - 'x'];
    return status;
  }
  if (!strcmp(n, "stop_motion") || !strcmp(n, "world_lift")) {
    if (count && fabsf(args[0].as.f32) > 30)
      return DM_EBOUNDS;
    u32 bodies = read32(h, 26417664);
    if (bodies > 16384)
      return DM_EBOUNDS;
    for (u32 i = 0, b; i < bodies; i++) {
      DMStatus status = body_at(h, i, &b);
      if (status != DM_OK)
        return status;
      if (!(get_float(h, b + 368) > 0))
        continue;
      if (count) {
        put_float(h, b + 340, get_float(h, b + 340) + args[0].as.f32);
        write32(h, b + 284, read32(h, b + 284) + 1);
      } else {
        memset(mem(h, b + 336, 32), 0, 32);
        memset(mem(h, b + 436, 32), 0, 32);
        write32(h, b + 284, read32(h, b + 284) + 1);
      }
      wake(h, b);
    }
    return DM_OK;
  }
  u32 b;
  DMStatus status = body_at(h, args[0].as.i32, &b);
  if (status != DM_OK)
    return status;
  if (!strncmp(n, "body_", 5) && count == 1) {
    u32 offset = !strcmp(n, "body_x")                ? 52
                 : !strcmp(n, "body_y")              ? 56
                 : !strcmp(n, "body_z")              ? 60
                 : !strcmp(n, "body_vx")             ? 336
                 : !strcmp(n, "body_vy")             ? 340
                 : !strcmp(n, "body_vz")             ? 344
                 : !strcmp(n, "body_wx")             ? 352
                 : !strcmp(n, "body_wy")             ? 356
                 : !strcmp(n, "body_wz")             ? 360
                 : !strcmp(n, "body_friction_value") ? 224
                 : !strcmp(n, "body_bounce_value")   ? 228
                                                     : 368;
    out->as.f32 = get_float(h, b + offset);
    return DM_OK;
  }
  if (count == 2) {
    float v = args[1].as.f32;
    if (v < 0 || v > (!strcmp(n, "body_friction") ? 10 : 1))
      return DM_EBOUNDS;
    put_float(h, b + (!strcmp(n, "body_friction") ? 224 : 228), v);
    write32(h, b + 284, read32(h, b + 284) + 1);
    wake(h, b);
    return DM_OK;
  }
  if (!(get_float(h, b + 368) > 0))
    return DM_EPROTECTED;
  for (u32 i = 1; i < 4; i++)
    if (fabsf(args[i].as.f32) > 200)
      return DM_EBOUNDS;
  bool force = !strcmp(n, "body_force");
  u32 offset = force ? 436 : !strcmp(n, "body_spin") ? 352 : 336;
  for (u32 i = 0; i < 3; i++) {
    float v = args[i + 1].as.f32;
    if (force)
      v = get_float(h, b + offset + i * 4) + v * get_float(h, b + 372 + i * 4);
    put_float(h, b + offset + i * 4, v);
  }
  write32(h, b + offset + 12, 0);
  if (!force)
    write32(h, b + 284, read32(h, b + 284) + 1);
  wake(h, b);
  return DM_OK;
}
