#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void fail(const char *message) {
  fprintf(stderr, "drive-mad: %s\n", message);
  exit(1);
}
void *mem(struct w2c_a *h, u32 p, size_t n) {
  wasm_rt_memory_t *m = w2c_game_ib(&h->game);
  if ((uint64_t)p > m->size || n > m->size - p)
    fail("Host adapter: out-of-bounds memory");
  return m->data + p;
}
const char *str(struct w2c_a *h, u32 p) {
  wasm_rt_memory_t *m = w2c_game_ib(&h->game);
  const char *s = mem(h, p, 1);
  if (!memchr(s, 0, m->size - p))
    fail("Host adapter: unterminated string");
  return s;
}
u32 read32(struct w2c_a *h, u32 p) {
  u32 v;
  memcpy(&v, mem(h, p, 4), 4);
  return v;
}
void write32(struct w2c_a *h, u32 p, u32 v) { memcpy(mem(h, p, 4), &v, 4); }
void write64(struct w2c_a *h, u32 p, u64 v) { memcpy(mem(h, p, 8), &v, 8); }
u32 copy_string(struct w2c_a *h, const char *s) {
  size_t n = strlen(s) + 1;
  u32 p = game_malloc(&h->game, n);
  if (!p)
    fail("Guest allocation failed");
  memcpy(mem(h, p, n), s, n);
  return p;
}
static wasm_rt_funcref_t ref(struct w2c_a *h, u32 index,
                             wasm_rt_func_type_t type) {
  wasm_rt_funcref_table_t *table = w2c_game_kb(&h->game);
  if (index >= table->size)
    fail("Invalid callback index");
  wasm_rt_funcref_t r = table->data[index];
  if (!r.func || !r.func_type || memcmp(r.func_type, type, 32))
    fail("Invalid callback signature");
  return r;
}
void call_v0(struct w2c_a *h, u32 index) {
  wasm_rt_funcref_t r = ref(h, index, wasm2c_game_get_func_type(0, 0));
  ((void (*)(void *))r.func)(r.module_instance);
}
void call_v3(struct w2c_a *h, u32 index, u32 a, u32 b, u32 c) {
  if (!index)
    return;
  wasm_rt_funcref_t r = ref(
      h, index,
      wasm2c_game_get_func_type(3, 0, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32));
  ((void (*)(void *, u32, u32, u32))r.func)(r.module_instance, a, b, c);
}
static void key(struct w2c_a *h, int glfw, int dom, int scan, int action) {
  if (!h->key_callback || glfw < 0)
    return;
  wasm_rt_funcref_t r =
      ref(h, h->key_callback,
          wasm2c_game_get_func_type(5, 0, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32,
                                    WASM_RT_I32, WASM_RT_I32));
  SDL_Keymod mods = SDL_GetModState();
  int m = ((mods & KMOD_SHIFT) ? 1 : 0) | ((mods & KMOD_CTRL) ? 2 : 0) |
          ((mods & KMOD_ALT) ? 4 : 0) | ((mods & KMOD_GUI) ? 8 : 0);
  ((void (*)(void *, u32, u32, u32, u32, u32))r.func)(r.module_instance, 1,
                                                      glfw, scan, action, m);
  if (glfw >= 0 && glfw < 349)
    h->keys[glfw] = action != 0;
  if (action && dom) {
    char text[16] = {0};
    if (glfw >= 32 && glfw <= 126)
      text[0] =
          (char)(glfw >= 'A' && glfw <= 'Z' && !(mods & KMOD_SHIFT) ? glfw + 32
                                                                    : glfw);
    else if (glfw >= 290 && glfw <= 301)
      snprintf(text, sizeof(text), "F%d", glfw - 289);
    const char *browser_key = glfw == 262   ? "ArrowRight"
                              : glfw == 263 ? "ArrowLeft"
                              : glfw == 265 ? "ArrowUp"
                              : glfw == 264 ? "ArrowDown"
                              : glfw == 256 ? "Escape"
                              : glfw == 257 ? "Enter"
                              : glfw == 258 ? "Tab"
                              : glfw == 259 ? "Backspace"
                              : glfw == 260 ? "Insert"
                              : glfw == 261 ? "Delete"
                              : glfw == 268 ? "Home"
                              : glfw == 269 ? "End"
                                            : text;
    u32 p = copy_string(h, browser_key);
    game_keydown_browser(&h->game, p);
    game_free(&h->game, p);
  }
  h->input_events++;
}
static int keycode(SDL_Keycode value, int *dom) {
  if (value >= 'a' && value <= 'z') {
    *dom = value - 'a' + 'A';
    return *dom;
  }
  if (value >= 32 && value <= 126) {
    *dom = value;
    return value;
  }
  switch (value) {
  case SDLK_ESCAPE:
    *dom = 27;
    return 256;
  case SDLK_RETURN:
    *dom = 13;
    return 257;
  case SDLK_TAB:
    *dom = 9;
    return 258;
  case SDLK_BACKSPACE:
    *dom = 8;
    return 259;
  case SDLK_INSERT:
    *dom = 45;
    return 260;
  case SDLK_DELETE:
    *dom = 46;
    return 261;
  case SDLK_RIGHT:
    *dom = 39;
    return 262;
  case SDLK_LEFT:
    *dom = 37;
    return 263;
  case SDLK_DOWN:
    *dom = 40;
    return 264;
  case SDLK_UP:
    *dom = 38;
    return 265;
  case SDLK_HOME:
    *dom = 36;
    return 268;
  case SDLK_END:
    *dom = 35;
    return 269;
  case SDLK_LSHIFT:
  case SDLK_RSHIFT:
    *dom = 16;
    return value == SDLK_LSHIFT ? 340 : 344;
  case SDLK_LCTRL:
  case SDLK_RCTRL:
    *dom = 17;
    return value == SDLK_LCTRL ? 341 : 345;
  case SDLK_LALT:
  case SDLK_RALT:
    *dom = 18;
    return value == SDLK_LALT ? 342 : 346;
  default:
    if (value >= SDLK_F1 && value <= SDLK_F12) {
      *dom = value - SDLK_F1 + 112;
      return value - SDLK_F1 + 290;
    }
    *dom = 0;
    return -1;
  }
}
void platform_release_keys(struct w2c_a *h) {
  memset(h->physical_keys, 0, sizeof(h->physical_keys));
  for (int i = 0; i < 349; i++)
    if (h->keys[i])
      key(h, i, 0, 0, 0);
  for (int i = 0; i < 3; i++)
    if (h->mouse_buttons[i]) {
      game_mouse_updown(&h->game, i, 0, 0, 0);
      h->mouse_buttons[i] = false;
    }
}
void platform_drive(struct w2c_a *h) {
  const int codes[] = {65, 68, 262, 263};
  bool desired[349] = {0};
  if (!menu_is_open(h)) {
    for (unsigned i = 0; i < 4; i++)
      if (h->physical_keys[codes[i]])
        desired[gameplay_key(h, codes[i])] = true;
    if (h->gameplay.auto_drive)
      desired[gameplay_key(h, h->gameplay.auto_drive > 0 ? 262 : 263)] = true;
  }
  for (unsigned i = 0; i < 4; i++) {
    int code = codes[i];
    if (desired[code] != h->keys[code])
      key(h, code, desired[code] ? code : 0, code, desired[code]);
  }
}
void platform_events(struct w2c_a *h) {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (menu_event(h, &e))
      continue;
    switch (e.type) {
    case SDL_QUIT:
      h->running = false;
      break;
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
      int dom = 0, code = keycode(e.key.keysym.sym, &dom);
      SDL_Keymod modifiers = SDL_GetModState();
      DMInput input = {.struct_size = sizeof(DMInput),
                       .type = DM_INPUT_KEY,
                       .key = code,
                       .scancode = e.key.keysym.scancode,
                       .action = e.type == SDL_KEYUP ? 0
                                 : e.key.repeat      ? 2
                                                     : 1,
                       .modifiers = ((modifiers & KMOD_SHIFT) ? 1 : 0) |
                                    ((modifiers & KMOD_CTRL) ? 2 : 0) |
                                    ((modifiers & KMOD_ALT) ? 4 : 0) |
                                    ((modifiers & KMOD_GUI) ? 8 : 0)};
      bool consumed = mods_input(h, &input);
      // A mod cannot strand a key already held by the game by consuming its
      // release.
      if (consumed &&
          !(input.action == 0 && code >= 0 && code < 349 && h->keys[code]))
        break;
      if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F11 &&
          !e.key.repeat) {
        SDL_SetWindowFullscreen(h->window, SDL_GetWindowFlags(h->window) &
                                                   SDL_WINDOW_FULLSCREEN_DESKTOP
                                               ? 0
                                               : SDL_WINDOW_FULLSCREEN_DESKTOP);
        break;
      }
      if (code >= 0 && code < 349)
        h->physical_keys[code] = e.type != SDL_KEYUP;
      if (code == 65 || code == 68 || code == 262 || code == 263)
        platform_drive(h);
      else
        key(h, code, dom, dom, e.type == SDL_KEYUP ? 0 : e.key.repeat ? 2 : 1);
      break;
    }
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
      int button = e.button.button == SDL_BUTTON_LEFT     ? 0
                   : e.button.button == SDL_BUTTON_MIDDLE ? 1
                                                          : 2;
      DMInput input = {.struct_size = sizeof(DMInput),
                       .type = DM_INPUT_BUTTON,
                       .key = button,
                       .action = e.type == SDL_MOUSEBUTTONDOWN,
                       .x = e.button.x,
                       .y = e.button.y};
      if (mods_input(h, &input) &&
          !(input.action == 0 && h->mouse_buttons[button]))
        break;
      game_mouse_updown(&h->game, button, e.button.x, e.button.y,
                        e.type == SDL_MOUSEBUTTONDOWN);
      h->mouse_buttons[button] = e.type == SDL_MOUSEBUTTONDOWN;
      h->input_events++;
      break;
    }
    case SDL_MOUSEMOTION: {
      DMInput input = {.struct_size = sizeof(DMInput),
                       .type = DM_INPUT_MOTION,
                       .x = e.motion.x,
                       .y = e.motion.y};
      if (mods_input(h, &input))
        break;
      game_mouse_move(&h->game, 0, e.motion.x, e.motion.y);
      break;
    }
    case SDL_MOUSEWHEEL: {
      DMInput input = {.struct_size = sizeof(DMInput),
                       .type = DM_INPUT_WHEEL,
                       .x = e.wheel.preciseX,
                       .y = e.wheel.preciseY};
      if (mods_input(h, &input))
        break;
      if (h->scroll_callback) {
        wasm_rt_funcref_t r =
            ref(h, h->scroll_callback,
                wasm2c_game_get_func_type(3, 0, WASM_RT_I32, WASM_RT_F64,
                                          WASM_RT_F64));
        ((void (*)(void *, u32, f64, f64))r.func)(
            r.module_instance, 1, e.wheel.preciseX, e.wheel.preciseY);
      }
      break;
    }
    case SDL_WINDOWEVENT:
      if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        h->width = e.window.data1;
        h->height = e.window.data2;
        call_v3(h, h->window_size_callback, 1, h->width, h->height);
        game_update_screen_size(&h->game, h->width, h->height, 1);
      } else if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        // Avoid stuck acceleration when a key is released in another window.
        if (!h->smoke_test)
          platform_release_keys(h);
        game_set_game_focus(&h->game, h->smoke_test ? 1 : 0);
      } else if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
        game_set_game_focus(&h->game, 1);
      else if (e.window.event == SDL_WINDOWEVENT_MINIMIZED && !h->smoke_test)
        gameplay_pause(h, true);
      else if (e.window.event == SDL_WINDOWEVENT_RESTORED)
        gameplay_pause(h, false);
      break;
    }
  }
}
u32 dm_glfwInit(struct w2c_a *h) { return 1; }
void dm_glfwTerminate(struct w2c_a *h) { h->running = false; }
void dm_glfwWindowHint(
    struct w2c_a *h, u32 a,
    u32 b) { /* GLES2 window is configured by the native host. */
}
u32 dm_glfwSetErrorCallback(struct w2c_a *h, u32 callback) { return 0; }
u32 dm_glfwGetPrimaryMonitor(struct w2c_a *h) { return 1; }
u32 dm_glfwGetVideoMode(struct w2c_a *h, u32 monitor) {
  u32 p = game_malloc(&h->game, 24);
  write32(h, p, h->width);
  write32(h, p + 4, h->height);
  write32(h, p + 8, 8);
  write32(h, p + 12, 8);
  write32(h, p + 16, 8);
  write32(h, p + 20, 60);
  return p;
}
u32 dm_glfwCreateWindow(struct w2c_a *h, u32 w, u32 height, u32 title,
                        u32 monitor, u32 share) {
  return h->window ? 1 : 0;
}
void dm_glfwMakeContextCurrent(struct w2c_a *h, u32 window) {
  SDL_GL_MakeCurrent(h->window, h->context);
}
void dm_glfwSwapInterval(struct w2c_a *h, u32 interval) {
  SDL_GL_SetSwapInterval(interval);
}
u32 dm_glfwSetWindowSizeCallback(struct w2c_a *h, u32 window, u32 callback) {
  u32 old = h->window_size_callback;
  h->window_size_callback = callback;
  return old;
}
u32 dm_glfwSetDropCallback(struct w2c_a *h, u32 window, u32 callback) {
  h->drop_callback = callback;
  return 0;
}
u32 dm_glfwSetScrollCallback(struct w2c_a *h, u32 window, u32 callback) {
  u32 old = h->scroll_callback;
  h->scroll_callback = callback;
  return old;
}
u32 dm_glfwSetKeyCallback(struct w2c_a *h, u32 window, u32 callback) {
  u32 old = h->key_callback;
  h->key_callback = callback;
  return old;
}
void dm_glfwSetClipboardString(struct w2c_a *h, u32 window, u32 text) {
  SDL_SetClipboardText(str(h, text));
}
void dm_glfwPollEvents(struct w2c_a *h) { /* Pumped outside the translated
                                             callback to avoid reentrancy. */
}
void dm_glfwSwapBuffers(
    struct w2c_a *h,
    u32 window) { /* Presented by main after optional screenshots. */
}
void dm_emscripten_set_main_loop(struct w2c_a *h, u32 callback, u32 fps,
                                 u32 simulate) {
  if (simulate)
    fail("Unexpected simulated infinite loop");
  h->main_loop = callback;
  h->paused = false;
}
void dm_emscripten_cancel_main_loop(struct w2c_a *h) { h->main_loop = 0; }
void dm_emscripten_pause_main_loop(struct w2c_a *h) { h->paused = true; }
void dm_emscripten_resume_main_loop(struct w2c_a *h) { h->paused = false; }
u32 dm_emscripten_request_fullscreen_strategy(struct w2c_a *h, u32 target,
                                              u32 defer, u32 strategy) {
  return SDL_SetWindowFullscreen(h->window, SDL_WINDOW_FULLSCREEN_DESKTOP)
             ? (u32)-1
             : 0;
}
u32 dm_get_hostname(struct w2c_a *h) { return copy_string(h, "localhost"); }
f32 dm_get_device_pixel_ratio(struct w2c_a *h) { return 1; }
u32 dm_get_url_level_index(struct w2c_a *h) { return h->level; }
u32 dm_is_latest_browser_tab(struct w2c_a *h) { return 1; }
u32 dm_is_daily_reward_possible(struct w2c_a *h) { return 1; }
void dm_set_latest_browser_tab(struct w2c_a *h) {}
f64 dm_emscripten_date_now(struct w2c_a *h) { return gameplay_clock(h); }
u32 dm_emscripten_get_now_is_monotonic(struct w2c_a *h) { return 1; }
void dm_emscripten_memcpy_js(struct w2c_a *h, u32 to, u32 from, u32 size) {
  memmove(mem(h, to, size), mem(h, from, size), size);
}
u32 dm_emscripten_resize_heap(struct w2c_a *h, u32 requested) {
  wasm_rt_memory_t *m = w2c_game_ib(&h->game);
  u64 pages = ((u64)requested + 65535) / 65536;
  if (pages <= m->pages)
    return 1;
  return wasm_rt_grow_memory(m, pages - m->pages) != (u64)-1;
}
void dm_assert_fail(struct w2c_a *h, u32 condition, u32 filename, u32 line,
                    u32 function) {
  fprintf(stderr, "Assertion: %s (%s:%u %s)\n", str(h, condition),
          str(h, filename), line, str(h, function));
  fail("Game assertion");
}
void dm_abort_js(struct w2c_a *h) { fail("Game aborted"); }
void dm_exit(struct w2c_a *h, u32 status) { exit(status); }
static void put_tm(struct w2c_a *h, u32 p, const struct tm *tm) {
#ifdef _WIN32
  const int gmtoff = 0; // MinGW's struct tm has no tm_gmtoff field.
#else
  const int gmtoff = (int)tm->tm_gmtoff;
#endif
  int values[] = {tm->tm_sec, tm->tm_min,   tm->tm_hour, tm->tm_mday,
                  tm->tm_mon, tm->tm_year,  tm->tm_wday, tm->tm_yday,
                  tm->tm_isdst, gmtoff};
  for (int i = 0; i < 10; i++)
    write32(h, p + i * 4, values[i]);
}
void dm_localtime_js(struct w2c_a *h, u32 lo, u32 hi, u32 p) {
  time_t t = (int64_t)(((u64)hi << 32) | lo);
  // The native host is single-threaded, so the static localtime result is safe
  // to copy. This also avoids MSVC/MinGW localtime_s argument-order differences.
  struct tm *local = localtime(&t);
  if (!local)
    fail("localtime failed");
  struct tm tm = *local;
  put_tm(h, p, &tm);
}
u32 dm_mktime_js(struct w2c_a *h, u32 p) {
  struct tm tm = {.tm_sec = (s32)read32(h, p),
                  .tm_min = (s32)read32(h, p + 4),
                  .tm_hour = (s32)read32(h, p + 8),
                  .tm_mday = (s32)read32(h, p + 12),
                  .tm_mon = (s32)read32(h, p + 16),
                  .tm_year = (s32)read32(h, p + 20),
                  .tm_isdst = (s32)read32(h, p + 32)};
  time_t t = mktime(&tm);
  put_tm(h, p, &tm);
  game_emscripten_tempret_set(&h->game, (u64)t >> 32);
  return t;
}
void dm_tzset_js(struct w2c_a *h, u32 zone, u32 daylight_ptr, u32 standard,
                 u32 summer) {
  tzset();
  write32(h, zone, timezone);
  write32(h, daylight_ptr, daylight);
  snprintf(mem(h, standard, 17), 17, "%.16s", tzname[0]);
  snprintf(mem(h, summer, 17), 17, "%.16s", tzname[1]);
}
static void audio_init(struct w2c_a *h, u32 samples, u32 rate) {
  if (h->audio)
    SDL_CloseAudioDevice(h->audio);
  h->audio_samples = samples;
  h->audio_rate = rate;
  h->audio_paused = false;
  SDL_AudioSpec want = {
      .freq = rate, .format = AUDIO_F32SYS, .channels = 1, .samples = 1024};
  h->audio = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
  if (h->audio)
    SDL_PauseAudioDevice(h->audio, 0);
  else
    fprintf(stderr, "Audio unavailable: %s\n", SDL_GetError());
}
u32 dm_emscripten_asm_const_int(struct w2c_a *h, u32 code, u32 sig, u32 args) {
  double a[8] = {0};
  const char *signature = str(h, sig);
  size_t n = strlen(signature);
  if (n > 8)
    fail("Too many inline bridge arguments");
  for (size_t i = 0; i < n; i++) {
    bool wide = signature[i] != 'i' && signature[i] != 'p';
    if (wide)
      args = (args + 7) & ~7u;
    if (wide)
      memcpy(&a[i], mem(h, args, 8), 8);
    else
      a[i] = (s32)read32(h, args);
    args += wide ? 8 : 4;
  }
  if (h->tracing)
    fprintf(stderr, "bridge %u (%s) %.0f %.0f %.0f\n", code, signature, a[0],
            a[1], a[2]);
  switch (code) {
  case 8464286:
    audio_init(h, a[0], a[1]);
    break;
  case 8464310:
    if (h->audio)
      SDL_CloseAudioDevice(h->audio);
    h->audio = 0;
    h->audio_samples = 0;
    break;
  case 8464330:
    h->audio_paused = true;
    if (h->audio)
      SDL_ClearQueuedAudio(h->audio);
    break;
  case 8464349:
    h->audio_paused = false;
    break;
  case 8465294:
    h->pending_init = 1;
    break;
  case 8465729:
    return h->width;
  case 8465779:
    return h->height;
  case 8465874:
    storage_init(h);
    break;
  case 8465896:
    storage_remove(h, a[0]);
    break;
  case 8465920:
    storage_write(h, a[0], a[1], a[2]);
    break;
  case 8464740:
  case 8464950:
    h->pending_ad |= 1;
    break;
  case 8464754:
  case 8464828:
    h->pending_ad |= 2;
    break;
  case 8464780:
  case 8464876:
    h->pending_ad |= 4;
    break;
  case 8464116:
    versus_fetch(h, str(h, a[0]), a[1], false);
    break;
  case 8464153:
    // sendUrl(url, request, bytes, length, flags): never send run uploads.
    versus_fetch(h, str(h, a[0]), a[1], true);
    break;
  case 8464197:
  case 8464248:
  case 8464016:
  case 8464964:
  case 8465006:
  case 8465081:
  case 8465242:
    fprintf(stderr,
            "Browser-only action %u is unavailable in the native port.\n",
            code);
    break;
  case 8465219:
    fprintf(stderr,
            "Restart requested; close and reopen the native executable.\n");
    break;
  // Browser overlay/analytics, unavailable hints, no-op local ad loading,
  // and syncfs (native file writes already persist).
  case 8464058:
  case 8464091:
  case 8464230:
  case 8464369:
  case 8464394:
  case 8464555:
  case 8464719:
  case 8464802:
  case 8464854:
  case 8465830:
  case 8465951:
  case 8466081:
  case 8466103:
  case 8466126:
  case 8466169:
  case 8466198:
    break;
  default:
    fprintf(stderr, "Unknown browser bridge address: %u\n", code);
    fail("Unsupported inline bridge; the binary ABI may have changed");
  }
  return 0;
}
void platform_tick(struct w2c_a *h) {
  if (h->pending_init) {
    h->pending_init = 0;
    game_app_init(&h->game);
    game_set_is_mobile(&h->game, 0);
    game_update_screen_size(&h->game, h->width, h->height, 1);
    game_set_game_focus(&h->game, 1);
  }
  u32 ads = h->pending_ad;
  h->pending_ad = 0;
  if (ads & 1)
    game_ad_on_inited(&h->game);
  if (ads & 2)
    game_ad_interstitial_on_showed(&h->game, 1);
  if (ads & 4) {
    game_ad_rewarded_on_showed(&h->game, 1);
    game_ad_rewarded_on_rewarded(&h->game);
  }
  if (h->audio_samples && !h->audio_paused) {
    for (int i = 0; i < 5; i++) {
      if (h->audio &&
          SDL_GetQueuedAudioSize(h->audio) >= h->audio_samples * 4 * 3)
        break;
      u32 data = game_webaudio_fill(&h->game);
      h->audio_buffers++;
      if (!h->audio)
        break;
      if (SDL_QueueAudio(h->audio, mem(h, data, (size_t)h->audio_samples * 4),
                         h->audio_samples * 4))
        fail("Cannot queue native audio");
    }
  }
}
