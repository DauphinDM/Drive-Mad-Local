# Drive Mad Native SDK 1.0

This is a real C ABI for **native C/C++ shared-library plugins** in the Linux game.
No JavaScript, WebView, scripting VM, generated WABT headers, or game rebuild is
needed to develop/load a plugin. Data-only asset packs are supported too.
The public contract is [`include/drive_mad.h`](include/drive_mad.h).
The **F2 / `--menu` native workbench** and 40 researched gameplay/host controls
are documented in [GAMEPLAY.md](GAMEPLAY.md), with evidence in
[GAMEPLAY-RESEARCH.md](GAMEPLAY-RESEARCH.md). The optional
`include/drive_mad_gameplay.h` adds ABI-1-compatible convenience wrappers.

**Security:** `.so` files execute with your full user permissions, including ELF
constructors before ABI validation. These are **trusted extensions, not sandboxed
mods**. API checks protect against common mistakes, not malicious native code or
corrupt game state. Only load mods you trust. No mods auto-load: explicitly pass
`--mods`. Never run the game/mods as root.

## Five-minute quick start (from this repository)

```bash
npm run native:build
npm run native:mod -- init native/build/mods/my-tools my-tools
npm run native:mod -- build native/build/mods/my-tools
./native/build/drive-mad --mods native/build/mods --console
```

Edit `mod.c` and `settings.ini` in the new folder. The scaffold includes a working
F6 reward hotkey, ad-frequency setting, window title, persistent launch counter,
and `info`, `pause`, `resume`, `reward` commands. In the terminal running the game:

```text
help
mods
exports ad_
mod my-tools info
mod my-tools reward
pause
resume
quit
```

The console is built into the native executable and reads newline-terminated
commands from standard input, without blocking rendering. It is **terminal-based,
separate from the F2 graphical workbench**. The same checked dispatcher is also
available in the menu's Mods / Console tab. Unknown commands report an error; arguments
are plain text, never shell commands. `--command 'mod my-tools info'` runs once
after readiness; repeat up to 32 times. `--self-test --command ...` runs commands
without graphics, but the core is deliberately **not** gameplay-ready.
Do not send gameplay commands until `on_ready`.

## Package layout and manifest

```text
mods/
  my-tools/
    mod.ini
    mod.so             # optional: absent library field = data-only pack
    settings.ini       # optional
    assets/...         # optional packed asset overrides
```

`mod.ini` and `settings.ini` are UTF-8 `key=value` files, no sections, quoting, or
inline comments. Blank lines and whole-line `#`/`;` comments are allowed. Values
are trimmed. Limits: 64 entries, 64 KiB/file, 1023-byte values. Duplicate/unknown
manifest fields are errors.

```ini
abi=1
id=my-tools
name=My Tools
version=1.0.0
game_version=166
library=mod.so
enabled=true
requires=shared-assets,another-mod
```

Required: `abi=1`, `id`, `version`. Name defaults to ID, enabled defaults to true.
Omit `library` for an asset-only mod; otherwise use a `.so` basename, no paths.
Omit `requires` for no dependencies. Dependencies are exact IDs, **not version
ranges**. Omit `game_version` (or set 0) to accept any core version; pin it for
mods using game pointers/semantics. Libraries must match manifest ID/version and
SDK ABI. A missing/disabled dependency, cycle, duplicate ID, failed load callback,
or incompatible manifest/library rejects startup rather than silently continuing.
At most 64 mod folders; non-directory files and dot-prefixed entries are ignored.

Ordering: folders sort lexically, then a stable topological sort places
dependencies first. The same order controls hooks and asset precedence (last
enabled matching override wins). Shutdown runs in reverse. `enabled=false`
disables both code and overrides. Restart after configuration, library, or asset
changes. **Hot unload/reload is intentionally not supported**: plugins can retain
GL resources, allocations and callbacks; unloading live code is unsafe.

## Lifecycle and threading

Export `DM_PUBLIC const DMMod *dm_mod_query(uint32_t host_abi)`. Return a static
descriptor, or NULL if ABI unsupported. The header supports C11 and C++17
(`extern "C"` included). Use `abi_version=DM_ABI_VERSION`,
`struct_size=sizeof(DMMod)`; the host validates both. The host supplies a
`const DMApi *`, borrowed `DMContext *`, and stores your `void *userdata`.

| Callback | When / purpose |
| --- | --- |
| `on_load(api, ctx, &userdata)` | After game constructors, before game main. Settings, storage, assets and memory are available; return `DM_OK`. |
| `on_ready(userdata)` | Exactly once after native platform/core initialization; gameplay exports become available. |
| `on_frame(userdata, frame)` | Before simulation each host tick, once ready; also while game simulation is paused. |
| `on_render(userdata, frame)` | After game drawing, before screenshots/present; GLES context current. |
| `on_input(userdata, input)` | Before game input, once ready. Nonzero consumes; first consumer wins. |
| `on_command(userdata, command, arguments)` | `mod <id> <command> [text]`, return a status. |
| `on_message(userdata, sender, topic, data, size)` | Queued cross-mod messages; payload borrowed for this callback only. |
| `on_unload(userdata)` | Reverse load order, game memory/context still alive; release everything you own. |

All API calls/callbacks are **main-thread only**, including audio-adjacent code.
Do not call the API from workers, ELF constructors/destructors, or after unload.
Do not retain callback argument pointers. Host/API/descriptor metadata pointers
remain valid until shutdown. Do not throw exceptions across the C ABI. A segfault,
trap, endless loop, or invalid call in a plugin can terminate/hang the process.

`DMFrame.delta_seconds` is real host time, not a simulation timestep.
`frame` counts completed game frames and may repeat while paused. Rendering hooks
may call GLES directly if linked with GLES; **save and restore all GL state you
change**, including framebuffer, program, buffers, attributes, textures, viewport,
blend/depth/stencil/scissor state. The host does not isolate rendering.

Input key codes follow GLFW (A=65, arrows=262–265, F1=290 … F12=301).
Physical `scancode` is SDL's scan code. Actions: up=0/down=1/repeat=2.
Modifiers: shift=1/ctrl=2/alt=4/super=8. Mouse buttons left=0/middle=1/right=2;
motion is window pixels, wheel is a delta. The host reserves quit/window lifecycle;
F11 fullscreen can be consumed. A release for a key already held by the game
is still forwarded to prevent stuck controls.

## Built-in API reference

All status-returning operations use `DM_OK=0` or negative `DMStatus`; use
`api->status_string(status)` for readable diagnostics. Check every result.
`struct_size` inputs must equal the current SDK struct size. All binary sizes are
bytes, all memory offsets are **32-bit translated game addresses, not C pointers**.

### State and exports

- `log(ctx, text)`: writes tagged diagnostics to stderr.
- `state(ctx, &state)`: initialize `DMState state={.struct_size=sizeof(state)}`;
  returns version/readiness/paused/size, frame/draw/audio/asset/input counters and uptime.
- `export_count`, `export_at`: enumerate 76 recovered core functions plus 40
  `native.*` gameplay/host entries with machine
  types/flags; metadata is borrowed, immutable. The generated
  `native/build/sdk-exports.json` records exact names/signatures.
- `call(ctx, name, args, count, result)`: checked native dispatch. Exact argument
  count and `DM_I32/I64/F32/F64` required; optional result pointer.
  `dm_i32`, `dm_i64`, `dm_f32`, `dm_f64`, `dm_call0` helpers are provided.
  Unknown name: `DM_ENOTFOUND`; wrong types/count: `DM_EABI`; before readiness:
  `DM_ENOTREADY`. Only `get_app_version`, `malloc`, `free` and
  `native.profile_supported` are callable early.
  Lifecycle/host-owned exports carry `DM_EXPORT_HOST_ONLY` and return
  `DM_EPROTECTED` (`main`, `app_init`, initial acceptance, termination, audio fill,
  storage replay and GL resume).

```c
DMValue version;
DMStatus s = api->call(ctx, "get_app_version", NULL, 0, &version);
DMValue frequency = dm_i32(0);
s = api->call(ctx, "set_ad_freq", &frequency, 1, NULL); /* after on_ready */
s = dm_call0(api, ctx, "app_pause");
s = dm_call0(api, ctx, "app_resume");
```

These are recovered **machine signatures**, not high-level semantic guarantees.
Many `i32` arguments are pointers, IDs, booleans, or enum values, not arbitrary
integers. Inspect WAT/pseudocode/the existing platform bridge first. Checked types
cannot validate those semantics or prevent core traps. Research-backed
`native.*` wrappers provide bounded gravity/body/material/level operations;
they fail closed outside the verified stock executable. There are **no invented
player-car/health/spawn APIs**. Change unresolved internal logic using the
function WAT workspace and rebuild, or reverse engineer it before wrapping it.

### Memory

- `allocate(ctx, bytes, &offset)` allocates in game memory (1 byte–16 MiB).
- `memory_read/write(ctx, offset, host_buffer, bytes)` bounds-check copies.
- `release(ctx, offset)` frees a game allocation. **Only free memory you own**;
  checking bounds cannot validate heap ownership. Do not retain native memory
  addresses across calls; translated memory can grow/move.

For a string parameter, allocate `strlen(text)+1`, copy including NUL, call the
export with `dm_i32(offset)`, then free only when the API's ownership semantics
permit it. Memory writes can corrupt the game despite bounds checks. Offsets are
version/build-specific; pin `game_version` and never guess physics offsets.

### Settings and storage

- `setting(ctx,key,buffer,capacity,&size)` reads your `settings.ini` snapshot.
  Size includes trailing NUL; missing keys return `DM_ENOTFOUND`.
- `storage_read/write/remove` operate on your own flat files under
  `$native_save_dir/mods/<mod-id>/`, separate from core/browser saves.
  Reads return exact bytes (no appended NUL); writes are atomic rename + fsync.
  Limits: 16 MiB/blob. Keys/IDs: 1–63 ASCII alphanumeric/`_-.`, no leading dot
  and no `..`. API storage uses no symlinks or path traversal.
- `setting`, `storage_read`, `asset_read` support size queries with
  `buffer=NULL,capacity=0`; `size` must be non-NULL. A buffer too small returns
  `DM_EBOUNDS` and required size. Missing files/keys return `DM_ENOTFOUND`.

### Assets and cross-mod messages

- `asset_count`, `asset_path` enumerate embedded game paths (261).
- `asset_read` reads effective **packed** bytes, including enabled overrides.
  Decompress zlib game/block/db payloads yourself when needed; PNG/WAV are raw.
- `publish(ctx,topic,data,size)` queues a message to all enabled plugins,
  including sender. Topics follow key rules. Max 4096 bytes, 64 queued messages;
  full returns `DM_EFULL`. Messages are dispatched next host tick; publications
  during a message callback wait another tick (no recursive dispatch).
  Publications from `on_ready` also wait until the following tick. Messages sent
  during `on_load` may arrive before gameplay is ready; check `state.ready`
  before calling gameplay exports from `on_message`.
- `window_title(ctx,text)` sets window title (max 255 bytes), or
  `DM_ENOTREADY` without a window. The host bundles Dear ImGui for its F2
  workbench; the standalone public C ABI does not expose ImGui objects or
  impose an ImGui binary ABI on plugins. Render hooks may use GLES themselves.

## Native asset packs (no plugin required)

Put overrides under the mod folder at the exact `assets/...` path. The native
filesystem feeds them to the original game without repacking the main executable.
Only known paths are overridden, not new files. Files/parent directories must
not be symlinks. Overrides are read-only and cached for the process lifetime;
max 16 MiB/file and 256 MiB total resolved overrides.

The core expects the original **packed/compressed format**. To take a decoded
file from `assets:unpack` and prepare it automatically:

```bash
npm run native:mod -- asset native/build/mods/my-pack \
  /assets/db modding/generated/unpacked/assets/db
```

PNG and WAV are copied unchanged; originally zlib-compressed assets are
recompressed. Bad dimensions/binary schemas/invalid game content can still crash
the original game; the loader does not understand unresolved game/block schemas.
Missing overrides fall back to embedded assets. A malformed/unreadable override
fails rather than silently pretending it loaded. Last enabled mod wins conflicts.

## Standalone SDK and build systems

```bash
npm run native:sdk                       # native/build/sdk, fresh output only
npm run native:sdk -- /absolute/new-sdk  # another distributable output
```

Includes headers, complete export signatures, this manual, CMake/package config,
pkg-config metadata, and sample plugins (`native-tools`, `fun-pack`,
`couch-chaos`, `time-warp`). Build the game first to generate export metadata.
No game sources or Node are needed by consumers:

```bash
cc -shared -fPIC -std=c11 -O2 -fvisibility=hidden \
  -I/path/to/sdk/include mod.c -o mod.so
# C++: c++ -shared -fPIC -std=c++17 ... mod.cpp -o mod.so

cmake -S /path/to/sdk/examples/native-tools -B /tmp/native-tools-build \
  -DCMAKE_PREFIX_PATH=/path/to/sdk
cmake --build /tmp/native-tools-build

PKG_CONFIG_PATH=/path/to/sdk/lib/pkgconfig pkg-config --cflags drive-mad-sdk
```

`fun-pack` demonstrates toy physics presets and hotkeys. `couch-chaos` is the
local turn-based two-player mode: copy it into a mods directory and build it
with `native:mod`; it is not a revival of the retired online Versus backend.
`time-warp` cycles the game clock and shows command argument parsing.

`find_package(DriveMadSDK 1 CONFIG REQUIRED)` provides `DriveMad::SDK`.
No host library link required; function pointers are supplied at load. The simple
`native:mod build` command supports `mod.c` or `mod.cpp`; use your own build system
for multi-file mods/additional libraries. SDL/GLES are optional plugin dependencies,
not part of the stable ABI. Libraries load locally with immediate symbol resolution
and no host-symbol exports. Depend on the API, not generated symbols.
Libraries are loaded through pinned `/proc/self/fd` paths; do not rely on `$ORIGIN`
to locate sibling shared libraries. Link system libraries normally or make the
plugin self-contained. Build for the user's Linux architecture/ABI.

Package with `npm run native:mod -- pack <mod-dir> /path/new-mod.tar.gz`.
The archive includes the whole folder, including sources/settings if present;
inspect it before sharing. Extract trusted archives yourself into an explicit
mods directory. The game does not auto-download/install archives or native code.
Generated `native/build/` contents are gitignored—keep your actual mod source
outside it or back it up/version-control it separately.

## Validation

`npm test` includes native SDK integration tests after `native:build`: C and C++
plugins, lifecycle/storage/settings/asset reads, export checks, malformed loaders,
dependency sorting and CLI workflows. `--self-test` does not initialize gameplay,
audio or GL. Use a graphical smoke test to verify real ready/frame/render/input:

```bash
DRIVE_MAD_SAVE_DIR=/tmp/opencode/drive-mad-sdk-test \
  ./native/build/drive-mad --mods native/build/mods --smoke-test \
  --command 'mod my-tools info'
```

The mod loader is a v1 SDK, not a promise that all game internals have been
reverse-engineered. API additions changing layouts require a new ABI version;
core changes may require re-auditing export semantics. No hot-reload, native-code
sandbox, graphical mod manager, high-level vehicle editor, or online workshop is
claimed.
