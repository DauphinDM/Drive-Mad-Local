#ifndef DRIVE_MAD_HOST_H
#define DRIVE_MAD_HOST_H

#include "abi.h"
#include "gameplay.h"
#include "sdk/include/drive_mad.h"
#include "gl_compat.h"
#include <SDL.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const char *path;
  uint32_t offset, size;
} NativeAsset;
extern const unsigned char native_assets[];
extern const NativeAsset native_asset_index[];
extern const size_t native_asset_count;

struct w2c_a {
  w2c_game game;
  SDL_Window *window;
  SDL_GLContext context;
  SDL_AudioDeviceID audio;
  u32 audio_samples, audio_rate, main_loop, window_size_callback;
  u32 key_callback, scroll_callback, drop_callback, pending_ad, pending_init;
  bool running, paused, audio_paused, tracing, smoke_test, menu_test;
  bool keys[349];
  bool physical_keys[349];
  bool mouse_buttons[3];
  int width, height, level;
  uint64_t frames, draw_calls, audio_buffers, asset_reads, input_events;
  char save_dir[4096];
  struct ModHost *mods;
  Gameplay gameplay;
  void *menu;
};

int dm_gl_load(void);
void *mem(struct w2c_a *h, u32 offset, size_t length);
const char *str(struct w2c_a *h, u32 offset);
u32 read32(struct w2c_a *h, u32 offset);
void write32(struct w2c_a *h, u32 offset, u32 value);
void write64(struct w2c_a *h, u32 offset, u64 value);
u32 copy_string(struct w2c_a *h, const char *value);
void fail(const char *message);
void call_v0(struct w2c_a *h, u32 index);
void call_v3(struct w2c_a *h, u32 index, u32 a, u32 b, u32 c);
void platform_events(struct w2c_a *h);
void platform_tick(struct w2c_a *h);
void filesystem_init(struct w2c_a *h);
void filesystem_close(void);
void filesystem_self_test(struct w2c_a *h);
void storage_init(struct w2c_a *h);
void storage_write(struct w2c_a *h, u32 path, u32 data, u32 size);
void storage_remove(struct w2c_a *h, u32 path);
int save_screenshot(struct w2c_a *h, const char *path);
extern const DMExport dm_exports[];
extern const uint32_t dm_export_count;
DMStatus mods_game_call(struct w2c_a *, const char *, const DMValue *, uint32_t,
                        DMValue *);
bool mods_ready(struct w2c_a *);
void mods_init(struct w2c_a *, const char *directory, bool console);
void mods_tick(struct w2c_a *, double delta);
void mods_render(struct w2c_a *);
bool mods_input(struct w2c_a *, const DMInput *);
void mods_console(struct w2c_a *);
DMStatus mods_command(struct w2c_a *, const char *);
void mods_close(struct w2c_a *);
void mods_unload(struct w2c_a *);
void mods_asset(struct w2c_a *, const char *, const unsigned char **, size_t *);
const DMApi *mods_api(void);
DMContext *mods_builtin_context(struct w2c_a *);
extern const DMExport gameplay_exports[];
extern const uint32_t gameplay_export_count;
DMStatus gameplay_call(struct w2c_a *, const char *, const DMValue *, uint32_t,
                       DMValue *);
void gameplay_init(struct w2c_a *);
void gameplay_tick(struct w2c_a *);
double gameplay_clock(struct w2c_a *);
void gameplay_after_frame(struct w2c_a *, bool stepped);
void gameplay_pause(struct w2c_a *, bool paused);
void platform_drive(struct w2c_a *);
void platform_release_keys(struct w2c_a *);
int gameplay_key(struct w2c_a *, int code);
void menu_init(struct w2c_a *, bool open);
void menu_close(struct w2c_a *);
bool menu_event(struct w2c_a *, const SDL_Event *);
void menu_render(struct w2c_a *);
void menu_log(struct w2c_a *, const char *sender, const char *text);
void menu_message(struct w2c_a *, const char *sender, const char *topic,
                  const void *, size_t);
void menu_set_open(struct w2c_a *, bool);
bool menu_is_open(struct w2c_a *);
DMStatus menu_select_tab(struct w2c_a *, const char *);
#ifdef __cplusplus
}
#endif

#endif
