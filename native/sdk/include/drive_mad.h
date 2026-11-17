/* Drive Mad Native SDK, ABI 1. No SDL, generated game, or WABT headers
 * required. */
#ifndef DRIVE_MAD_SDK_H
#define DRIVE_MAD_SDK_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define DM_ABI_VERSION 1u
#define DM_MAX_ARGS 16u
#define DM_EXPORT_EARLY 1u
#define DM_EXPORT_HOST_ONLY 2u
#if defined(_WIN32)
#define DM_PUBLIC __declspec(dllexport)
#elif defined(__GNUC__)
#define DM_PUBLIC __attribute__((visibility("default")))
#else
#define DM_PUBLIC
#endif

typedef struct DMContext
    DMContext; /* Borrowed per-mod context, main thread only. */
typedef enum {
  DM_OK = 0,
  DM_EINVAL = -1,
  DM_ENOTFOUND = -2,
  DM_EBOUNDS = -3,
  DM_ENOTREADY = -4,
  DM_EIO = -5,
  DM_EABI = -6,
  DM_EFULL = -7,
  DM_EPROTECTED = -8
} DMStatus;
typedef enum { DM_VOID, DM_I32, DM_I64, DM_F32, DM_F64 } DMType;
typedef struct {
  DMType type;
  union {
    uint32_t i32;
    uint64_t i64;
    float f32;
    double f64;
  } as;
} DMValue;
typedef struct {
  const char *name;
  uint32_t flags, argument_count;
  DMType arguments[DM_MAX_ARGS], result;
} DMExport;
typedef struct {
  uint32_t struct_size, game_version, ready, paused, width, height;
  uint64_t frames, draw_calls, audio_buffers, asset_reads, input_events;
  double uptime_seconds;
} DMState;
typedef struct {
  uint32_t struct_size;
  uint64_t frame;
  double delta_seconds, uptime_seconds;
} DMFrame;
typedef enum {
  DM_INPUT_KEY = 1,
  DM_INPUT_BUTTON,
  DM_INPUT_MOTION,
  DM_INPUT_WHEEL
} DMInputType;
typedef struct {
  uint32_t struct_size;
  DMInputType type;
  int32_t key, scancode, action; /* GLFW key codes; 0=up, 1=down, 2=repeat. */
  uint32_t modifiers;            /* Shift=1, Ctrl=2, Alt=4, Super=8. */
  float x,
      y; /* Mouse position in window pixels; wheel deltas for wheel events. */
} DMInput;

typedef struct DMApi {
  uint32_t abi_version, struct_size;
  const char *(*status_string)(DMStatus);
  void (*log)(DMContext *, const char *message);
  DMStatus (*state)(DMContext *, DMState *);
  uint32_t (*export_count)(DMContext *);
  const DMExport *(*export_at)(DMContext *, uint32_t);
  DMStatus (*call)(DMContext *, const char *, const DMValue *, uint32_t,
                   DMValue *);
  DMStatus (*memory_read)(DMContext *, uint32_t offset, void *, size_t);
  DMStatus (*memory_write)(DMContext *, uint32_t offset, const void *, size_t);
  DMStatus (*allocate)(DMContext *, uint32_t bytes, uint32_t *offset);
  DMStatus (*release)(DMContext *, uint32_t offset);
  /* Reads support a size query: data=NULL, capacity=0; *size is always
   * required. */
  DMStatus (*setting)(DMContext *, const char *key, char *, size_t,
                      size_t *size);
  DMStatus (*storage_read)(DMContext *, const char *key, void *, size_t,
                           size_t *);
  DMStatus (*storage_write)(DMContext *, const char *key, const void *, size_t);
  DMStatus (*storage_remove)(DMContext *, const char *key);
  uint32_t (*asset_count)(DMContext *);
  const char *(*asset_path)(DMContext *, uint32_t);
  DMStatus (*asset_read)(DMContext *, const char *, void *, size_t, size_t *);
  DMStatus (*publish)(DMContext *, const char *topic, const void *, size_t);
  DMStatus (*window_title)(DMContext *, const char *);
} DMApi;

typedef struct DMMod {
  uint32_t abi_version, struct_size;
  const char *id, *name, *version;
  DMStatus (*on_load)(const DMApi *, DMContext *, void **userdata);
  void (*on_ready)(void *);
  void (*on_frame)(void *, const DMFrame *); /* Before game simulation. */
  void (*on_render)(
      void *, const DMFrame *); /* After game drawing; restore GL state! */
  int (*on_input)(void *, const DMInput *); /* Nonzero consumes event. */
  DMStatus (*on_command)(void *, const char *command, const char *arguments);
  void (*on_message)(void *, const char *sender, const char *topic,
                     const void *data, size_t size);
  void (*on_unload)(void *);
} DMMod;
typedef const DMMod *(*DMQuery)(uint32_t host_abi);
/* Every shared library exports this symbol; return NULL for unsupported ABI. */
DM_PUBLIC const DMMod *dm_mod_query(uint32_t host_abi);

static inline DMValue dm_i32(uint32_t v) {
  DMValue x = {DM_I32, {0}};
  x.as.i32 = v;
  return x;
}
static inline DMValue dm_i64(uint64_t v) {
  DMValue x = {DM_I64, {0}};
  x.as.i64 = v;
  return x;
}
static inline DMValue dm_f32(float v) {
  DMValue x = {DM_F32, {0}};
  x.as.f32 = v;
  return x;
}
static inline DMValue dm_f64(double v) {
  DMValue x = {DM_F64, {0}};
  x.as.f64 = v;
  return x;
}
static inline DMStatus dm_call0(const DMApi *api, DMContext *ctx,
                                const char *name) {
  return api->call(ctx, name, NULL, 0, NULL);
}
#ifdef __cplusplus
}
#endif
#endif
