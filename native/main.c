#include "host.h"
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void gl_debug(unsigned source, unsigned type, unsigned id,
                     unsigned severity, int length, const char *message,
                     const void *user) {
  if (type == 0x824c) // GL_DEBUG_TYPE_ERROR, available with KHR_debug.
    fprintf(stderr, "Native GL error: %.*s\n", length, message);
}

int save_screenshot(struct w2c_a *h, const char *path) {
  int w, hp;
  SDL_GL_GetDrawableSize(h->window, &w, &hp);
  size_t bytes = (size_t)w * hp * 4;
  unsigned char *pixels = malloc(bytes), *flipped = malloc(bytes);
  if (!pixels || !flipped)
    fail("Screenshot allocation failed");
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, w, hp, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  for (int y = 0; y < hp; y++)
    memcpy(flipped + (size_t)y * w * 4, pixels + (size_t)(hp - 1 - y) * w * 4,
           (size_t)w * 4);
  png_image image = {.version = PNG_IMAGE_VERSION,
                     .width = w,
                     .height = hp,
                     .format = PNG_FORMAT_RGBA};
  int ok = png_image_write_to_file(&image, path, 0, flipped, 0, NULL);
  if (!ok)
    fprintf(stderr, "Screenshot failed: %s\n", image.message);
  png_image_free(&image);
  free(pixels);
  free(flipped);
  return ok;
}
int main(int argc, char **argv) {
  struct w2c_a h = {.width = 1280, .height = 720, .running = true};
  int smoke = 0;
  bool self_test = false;
  const char *screenshot = NULL;
  const char *mod_directory = NULL, *commands[32];
  unsigned command_count = 0;
  bool console = false, show_menu = false;
  gameplay_init(&h);
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--trace"))
      h.tracing = true;
    else if (!strcmp(argv[i], "--smoke-test"))
      smoke = 180, h.smoke_test = true;
    else if (!strcmp(argv[i], "--self-test"))
      self_test = true;
    else if (!strcmp(argv[i], "--mods") && i + 1 < argc)
      mod_directory = argv[++i];
    else if (!strcmp(argv[i], "--console"))
      console = true;
    else if (!strcmp(argv[i], "--menu"))
      show_menu = true;
    else if (!strcmp(argv[i], "--menu-self-test"))
      show_menu = h.menu_test = h.smoke_test = true, smoke = 180;
    else if (!strcmp(argv[i], "--command") && i + 1 < argc) {
      if (command_count == 32)
        fail("At most 32 startup commands");
      commands[command_count++] = argv[++i];
    } else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc)
      screenshot = argv[++i];
    else if (!strcmp(argv[i], "--help")) {
      puts(
          "drive-mad [--smoke-test | --self-test | --menu-self-test] "
          "[--screenshot output.png] "
          "[--trace] [--mods directory] [--console] [--menu] [--command text]\n"
          "Native mods are trusted .so code; none load unless --mods is "
          "supplied.\nArrow "
          "keys / A,D: drive. Mouse: menus. F2: native workbench. F11: "
          "fullscreen.\nNative saves: "
          "$XDG_DATA_HOME/drive-mad-native (or DRIVE_MAD_SAVE_DIR).\nNo "
          "browser or network; local reward callbacks are enabled.");
      return 0;
    } else {
      fprintf(stderr, "Unknown option: %s\n", argv[i]);
      return 2;
    }
  }
  if (self_test && !getenv("DRIVE_MAD_SAVE_DIR"))
    fail("--self-test requires a dedicated DRIVE_MAD_SAVE_DIR");
  wasm_rt_init();
  filesystem_init(&h);
  if (self_test) {
    wasm2c_game_instantiate(&h.game, &h);
    w2c_game_jb(&h.game);
    mods_init(&h, mod_directory, console);
    filesystem_self_test(&h);
    for (unsigned i = 0; i < command_count; i++)
      if (mods_command(&h, commands[i]) != DM_OK)
        fail("Startup mod command failed");
    mods_tick(&h, 0);
    printf("Native self-test passed. Compiled game version: %u\n",
           game_get_app_version(&h.game));
    mods_unload(&h);
    wasm2c_game_free(&h.game);
    filesystem_close();
    mods_close(&h);
    wasm_rt_free();
    return 0;
  }
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS))
    fail(SDL_GetError());
  // CI and software-rendering setups can point SDL at an alternate GL library.
  // Desktop Windows otherwise always resolves opengl32.dll from System32.
  const char *gl_library = getenv("DRIVE_MAD_GL_LIBRARY");
  if (gl_library && SDL_GL_LoadLibrary(gl_library))
    fail(SDL_GetError());
#ifdef DM_DESKTOP_GL
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                      SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#else
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  h.window = SDL_CreateWindow("Drive Mad — Native", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, h.width, h.height,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  if (!h.window)
    fail(SDL_GetError());
  h.context = SDL_GL_CreateContext(h.window);
  if (!h.context)
    fail(SDL_GetError());
  if (!dm_gl_load())
    fail("Desktop OpenGL 2.1 entry points are unavailable");
  fprintf(stderr, "Native GPU: %s / %s\n", glGetString(GL_RENDERER),
          glGetString(GL_VERSION));
  // Some SDL/driver context probes leave an error. Start the game's GL work
  // with a clean baseline; errors from translated game calls are not cleared.
  for (GLenum setup_error; (setup_error = glGetError()) != GL_NO_ERROR;)
    if (h.tracing)
      fprintf(stderr, "SDL context setup GL error: 0x%x (cleared)\n",
              setup_error);
  if (SDL_GL_ExtensionSupported("GL_KHR_debug")) {
    typedef void (*DebugCallback)(void (*)(unsigned, unsigned, unsigned,
                                           unsigned, int, const char *,
                                           const void *),
                                  const void *);
    DebugCallback debug =
        (DebugCallback)SDL_GL_GetProcAddress("glDebugMessageCallbackKHR");
    if (debug) {
      debug(gl_debug, NULL);
      glEnable(0x92e0);
      glEnable(0x8242);
    }
  }
  wasm2c_game_instantiate(&h.game, &h);
  w2c_game_jb(&h.game); // Original __wasm_call_ctors export; not main().
  fprintf(stderr, "Compiled game version: %u\n", game_get_app_version(&h.game));
  mods_init(&h, mod_directory, console);
  menu_init(&h, show_menu);
  if (game_main(&h.game, 0, 0))
    fail("Game main failed");
  game_user_accepted_and_clicked(&h.game);
  bool image_saved = false;
  uint64_t boot = SDL_GetTicks64();
  uint64_t previous = boot;
  bool commands_ran = false;
  while (h.running) {
    uint64_t start = SDL_GetTicks64();
    platform_events(&h);
    platform_tick(&h);
    gameplay_tick(&h);
    mods_tick(&h, (start - previous) / 1000.0);
    previous = start;
    if (mods_ready(&h) && !commands_ran) {
      for (unsigned i = 0; i < command_count; i++)
        if (mods_command(&h, commands[i]) != DM_OK)
          fail("Startup mod command failed");
      commands_ran = true;
    }
    mods_console(&h);
    bool stepped = h.paused && h.gameplay.steps;
    if (h.main_loop && (!h.paused || stepped)) {
      if (stepped)
        gameplay_pause(&h, false);
      call_v0(&h, h.main_loop);
      h.frames++;
      gameplay_after_frame(&h, stepped);
    }
    mods_render(&h);
    menu_render(&h);
    if (smoke && h.frames == 60) {
      // Exercise the same event path used by actual keyboard input.
      SDL_Event event = {.type = SDL_KEYDOWN};
      event.key.keysym.sym = SDLK_RIGHT;
      event.key.keysym.scancode = SDL_SCANCODE_RIGHT;
      SDL_PushEvent(&event);
    }
    if (smoke && h.frames == 120) {
      SDL_Event event = {.type = SDL_KEYUP};
      event.key.keysym.sym = SDLK_RIGHT;
      SDL_PushEvent(&event);
    }
    if (smoke && h.frames >= (uint64_t)smoke) {
      if (screenshot)
        image_saved = save_screenshot(&h, screenshot);
      h.running = false;
    }
    SDL_GL_SwapWindow(h.window);
    uint64_t elapsed = SDL_GetTicks64() - start;
    if (elapsed < 16)
      SDL_Delay(16 - elapsed);
    if (smoke && SDL_GetTicks64() - boot > 30000) {
      fprintf(stderr, "frames=%llu callback=%u paused=%d\n",
              (unsigned long long)h.frames, h.main_loop, h.paused);
      fail("Smoke test exceeded 30 seconds");
    }
  }
  if (screenshot && !smoke)
    image_saved = save_screenshot(&h, screenshot);
  unsigned error = glGetError();
  fprintf(stderr,
          "Native run: frames=%llu draws=%llu assets=%llu audio_buffers=%llu "
          "input=%llu GL_error=0x%x\n",
          (unsigned long long)h.frames, (unsigned long long)h.draw_calls,
          (unsigned long long)h.asset_reads,
          (unsigned long long)h.audio_buffers,
          (unsigned long long)h.input_events, error);
  bool pass =
      !smoke ||
      (h.frames >= 180 && h.draw_calls > 0 && h.asset_reads > 0 &&
       (h.input_events >= 2 || menu_is_open(&h)) &&
       game_get_app_inited(&h.game) && !error && (!screenshot || image_saved));
  mods_unload(&h);
  menu_close(&h);
  game_app_terminate_if_necessary(&h.game);
  if (h.audio)
    SDL_CloseAudioDevice(h.audio);
  wasm2c_game_free(&h.game);
  filesystem_close();
  mods_close(&h);
  SDL_GL_DeleteContext(h.context);
  SDL_DestroyWindow(h.window);
  SDL_Quit();
  wasm_rt_free();
  return pass ? 0 : 1;
}
