# Drive Mad — native

Drive Mad compiled to a native executable. The game’s decompiled WebAssembly is
translated to C with `wasm2c`, linked with a small SDL/OpenGL host, and shipped
with its assets embedded. There is no Tauri, Electron, WebView, browser runtime,
JavaScript engine or WebAssembly interpreter in the built game.

- **Linux:** x86-64 ELF (AppImage and portable tarball)
- **Windows:** x64 executable and NSIS installer
- **macOS:** Apple-silicon app and DMG

Download the latest packages from
[GitHub Releases](https://github.com/DauphinDM/Drive-Mad-Local/releases).

## Run

```sh
./native/build/drive-mad          # Linux build from source
./native/build/drive-mad --menu   # open the mod workbench (F2 also toggles it)
```

Linux packages install to `~/.local`:

```sh
tar -xzf DriveMad-*-linux-x86_64.tar.gz
cd DriveMad-*-linux-x86_64
./install.sh
```

Saves live in `~/.local/share/drive-mad-native` on Linux,
`%LOCALAPPDATA%\drive-mad-native` on Windows and
`~/.local/share/drive-mad-native` on macOS. Override with
`DRIVE_MAD_SAVE_DIR`. Online ads, leaderboards, web views and browser downloads
are unavailable offline; the local reward callback remains available.

## Build from source

Use Node.js 22+, a C11/C++17 compiler, CMake and the SDL2, OpenGL (or GLES2)
and libpng development packages.

```sh
npm install
npm run tools:install     # pinned WABT 1.0.39 (wasm-decompile + wasm2c)
npm run decompile         # original bundle -> editable WAT + pseudocode
npm run native:build      # WAT -> C -> native executable
npm run native:run
```

The editable `webapp/index.wasm`, `index.js` and `index.data` files are kept as
build inputs. They are not served as a web app.

## Modding

- **Native C/C++ plugins:** [SDK and built-in API](native/sdk/README.md). Mods
  load at runtime with lifecycle/input/render hooks, checked game exports,
  settings/storage, asset packs, messages and terminal commands.
- **Gameplay and API workbench:** press **F2** or pass `--menu`. It exposes
  researched gravity, velocity, force, material and level operations plus the
  full 116-entry API browser.
- **Assets:** `npm run assets:unpack`, edit images/audio/metadata, then
  `npm run assets:pack` and rebuild.
- **Compiled logic:** [decompilation and function editing](modding/README.md).
  Edit a function file, run `npm run wasm:build`, then `npm run native:build`.

The decompiled pseudocode is not original source code. Game/level binaries
remain custom formats. Physics operations act on researched engine state and
the whole world, not an invented player-car API. The gameplay profile fails
closed after meaningful WASM logic edits.

## Verification

```sh
npm test
```

On Linux this includes real native startup, filesystem/save isolation, plugin
loading, physics changes, the eight-tab workbench, and packaged SDK tests.
See [native/README.md](native/README.md) for CI, packaging and platform notes.
