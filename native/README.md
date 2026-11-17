# Native builds (Linux, Windows, macOS)

This target compiles the **editable decompiled WAT** to C with WABT `wasm2c`,
then compiles that C to a native executable. SDL handles the window,
keyboard/mouse and audio. Linux uses OpenGL ES 2; Windows and macOS use desktop
OpenGL 2.1 with a GLSL compatibility layer. There is **no Tauri,
Electron, WebView, JavaScript engine, WASM interpreter or web server** at runtime.
The small WABT C support library implements translated memory/table semantics
and traps; it is not an interpreter.

The analysis-only `index.dcmp` pseudocode cannot be compiled as C. This build uses
`modding/generated/index.wat` and, when present, the edited individual files in
`modding/generated/workspace/functions/`.

## Build

Linux/Arch prerequisites (install these yourself if missing):

```bash
sudo pacman -S --needed base-devel pkgconf sdl2-compat libglvnd mesa libpng curl zlib nodejs npm
```

macOS uses `brew install sdl2 libpng pkg-config curl zlib` (put Homebrew curl
and zlib's `lib/pkgconfig` directories on `PKG_CONFIG_PATH`); Windows CI uses MSYS2 with
`mingw-w64-x86_64-{gcc,SDL2,libpng,pkgconf,nodejs,curl,zlib}`. Building needs C11 and C++17
compilers and `pkg-config` packages `sdl2`, `libpng`, `libcurl`, `zlib`; Linux adds `glesv2` (or
`gl` for the desktop-GL validation build). No Rust, Qt or web runtime is needed.

```bash
npm install
npm run tools:install
npm run decompile       # first time only; does not overwrite existing WAT edits
npm run native:build
./native/build/drive-mad
```

If decompilation is already present, **skip** `decompile`. The build always merges
your current function workspace and checks the original import/export signatures
before translating. `npm run native:build -- --monolithic` explicitly ignores the
function workspace and uses the monolithic WAT instead. See the
[modding guide](../modding/README.md) for editing and backing up generated files.

`JOBS=2 npm run native:build` limits concurrent compiler processes.
`CC=clang CXX=clang++` selects another compiler pair. For a custom WABT installation set absolute
paths `WASM2C`, `WASM2C_RUNTIME` (directory of runtime C sources), and
`WASM2C_INCLUDE` (directory containing `wasm-rt.h`); use matching versions.

Output: `native/build/drive-mad` (or `drive-mad.exe`), a dynamically linked
binary for the machine that built it, plus generated C and `build.json`
recording source/asset hashes.
All 261 packed assets are embedded; the executable can run outside this repo
without `.wasm`, `.data`, HTML or JavaScript files. Normal system SDL, graphics,
C/C++, PNG, curl and zlib shared libraries are still required. Release installers bundle the
non-driver libraries they need; GPU/display drivers stay on the host.

## Controls and saves

- Arrow keys / A,D: drive; mouse: menus.
- **F2**: native mod workbench; `--menu` opens it on launch. Escape closes it
  when not editing text. It captures input before plugins and the game.
- F11: toggle fullscreen; close the window to quit.
- `--versus` or **F2 → Fun → Enable replay downloads and open Versus**:
  the original recorded ghost races. Downloads are opt-in; uploads are blocked.
  This is not yet simultaneous friend multiplayer. See
  [the multiplayer research](sdk/MULTIPLAYER-RESEARCH.md).
- Progress is separate from browser saves:
  `$XDG_DATA_HOME/drive-mad-native`, or `~/.local/share/drive-mad-native` if
  unset on Linux/macOS; `%LOCALAPPDATA%\drive-mad-native` on Windows.
- Set `DRIVE_MAD_SAVE_DIR=/absolute/path` to use another save directory.
- Embedded `/assets` files are read-only. Native writable `/sandbox` and `/tmp`
  are mapped under that save directory, not your system root.

Make shared game-logic changes in WAT, shared asset changes with `assets:pack`,
and native platform changes in `native/{main,platform,graphics,filesystem,mods,gameplay}.c`
and `native/menu.cpp`.

## Gameplay controls and native menu

The engine's own menus are compiled Fancade scenes inside the game assets, not a
reusable native widget toolkit. The overlay therefore stays a native Dear ImGui
surface, but uses the shipped `default.png` atlas for its title mark and toolbar
buttons instead of generic chrome.

The built-in nine-tab Dear ImGui workbench includes actual gameplay alterations:
gravity presets/custom vectors, body velocity/spin/forces/materials, stop/lift the
physics world, runtime level selection/restart/next, game-clock scale, pause/frame
step, auto drive and inverted controls. It also exposes typed API calls, assets,
owned memory buffers, private settings/storage, messages, mod commands and diagnostics.

```bash
./native/build/drive-mad --menu
./native/build/drive-mad --command "api native.time_scale 0.5"
```

See [the full menu/API reference](sdk/GAMEPLAY.md) and
[reverse-engineering evidence](sdk/GAMEPLAY-RESEARCH.md). Physics/level controls
require the stock-v166 executable profile; modified logic disables them rather
than guessing offsets. World-body tools are **not a player-car selector**.
Time scale affects game UI/timers as well as simulation; step means one host
frame, not guaranteed one physics tick. Back up saves before experimentation.

Dear ImGui v1.91.9b is vendored at a pinned upstream commit with its MIT license
in `native/vendor/imgui/LICENSE.txt`; provenance/hashes are in `source.json`.
The menu uses the shipped game atlas for its toolbar buttons and title mark,
and the sleek Space Grotesk UI font (SIL OFL 1.1, license at
`native/vendor/fonts/OFL.txt`). No extra GUI runtime/package is installed. `npm run native:ui:install` validates
existing vendor files, or downloads the pinned files if absent; it refuses to
overwrite edits/partial installs.

## Native mods and SDK

The native game supports opt-in C/C++ `.so` plugins, data-only asset packs,
lifecycle/input/render hooks, checked game-export calls, namespaced storage,
settings, cross-mod messaging and a built-in terminal console.
See the [complete native SDK guide](sdk/README.md) for the API and examples,
including the `fun-pack`, `time-warp` and local `couch-chaos` multiplayer mods.

```bash
npm run native:mod -- init native/build/mods/my-tools
npm run native:mod -- build native/build/mods/my-tools
./native/build/drive-mad --mods native/build/mods --console
npm run native:sdk  # export standalone headers/CMake/pkg-config/examples
```

Native plugins are trusted code, **not sandboxed**; none load without `--mods`.
On Windows, use the GUI console or `--command`; interactive `--console` stdin
polling is available on Linux/macOS.

## Offline limitations

The core game runs locally. HTTP requests fail explicitly; online accounts,
leaderboards/cloud sync, web views, browser downloads/share dialogs and external
links are not implemented. Local reward callbacks follow the browser project's
existing no-ad behavior. Browser overlays/debug menus are not included.
The native workbench is independent of those browser overlays.

The host adapter is bound to this game's recovered ABI and inline-JavaScript
addresses. Unknown bridge operations fail explicitly rather than silently
pretending to succeed. Replacing the original game binary requires auditing the
adapter as well as regenerating the decompilation.

## Smoke test

Requires an available graphical session and working GLES2 driver:

```bash
DRIVE_MAD_SAVE_DIR=/tmp/opencode/drive-mad-native-test \
  ./native/build/drive-mad --smoke-test --screenshot /tmp/opencode/drive-mad-native.png
```

Runs 180 game frames, injects a right-key press/release, captures the game framebuffer,
prints rendering/asset/audio/input counters and exits nonzero on failed checks.
`--trace` logs filesystem opens and recovered bridge calls for debugging.

`--menu-self-test` exercises the real native UI pause/step/resume buttons and all
nine tabs with synthetic ImGui input (without warping the desktop pointer).
Use `DRIVE_MAD_TEST_GUI=1 OPENCODE_TMP_DIR=/tmp/opencode npm test` in a graphical
session for live ABI-1 gameplay/physics and UI tests as well as native SDK tests.

`npm test` also runs a headless native integration test when the binary exists:
it launches the ELF outside the repository and checks embedded PNG reads,
read-only assets, seek/read/write/stat, save persistence, and path confinement.
It uses a temporary save directory. To invoke the check directly:

```bash
DRIVE_MAD_SAVE_DIR=/tmp/opencode/drive-mad-host-test ./native/build/drive-mad --self-test
```
