# Modifying Drive Mad

There are three layers: native plugins, packed assets, and compiled WASM. Use
the first two whenever possible; changing WASM requires low-level work.

## Setup

Use Node.js 22+ and a C/C++ toolchain with SDL2, OpenGL/GLES2 and libpng. From
the project root:

```sh
npm install
npm run tools:install
npm run decompile
npm run wasm:split
npm run assets:unpack
npm run native:build
```

You only need to decompile/unpack once. Neither command overwrites existing
generated edits.

`tools:install` downloads native **WABT 1.0.39** from the official GitHub release,
verifies its published SHA-256, and extracts it under `modding/generated/tools/`.
It needs `tar` on PATH. Supported downloads: Linux x64/arm64, macOS arm64,
Windows x64. For other platforms, install native WABT 1.0.39 yourself and set
`WASM_DECOMPILE` to the absolute `wasm-decompile` executable path. Later WABT
releases removed this tool. The npm build’s decompiler overflows its fixed WASM
stack on this game; the npm WAT parser/assembler works normally.

## 1. Native plugins — easiest

The versioned C/C++ SDK lives in [native/sdk](native/sdk/README.md). Plugins are
trusted native code loaded only with an explicit `--mods` directory; they can
read input, hook frames and rendering, call checked game exports, override
packed assets, and keep per-mod storage. They do not require rebuilding the game.

```sh
npm run native:mod -- init native/build/mods/my-tools
npm run native:mod -- build native/build/mods/my-tools
./native/build/drive-mad --mods native/build/mods --console
```

Export a standalone SDK with `npm run native:sdk`. See the SDK guide for the
manifest format, lifecycle, API limits, asset packs and CMake/pkg-config use.

## 2. Asset editing

```sh
npm run assets:unpack
```

The extraction contains **261 assets**:

| Path beneath `modding/generated/unpacked/assets/` | Contents |
| --- | --- |
| `atlas/` | 3 PNG atlases: `default.png`, `font.png`, `sprite.png` |
| `sounds/` | 23 WAV sounds |
| `db` | Zlib-decoded JSON metadata/database (no filename extension) |
| `leaderboards/` | 1 JSON file |
| `games/` | 14 zlib-decoded custom game/level binaries |
| `blocks/` | 219 zlib-decoded custom block definitions |

Open PNGs/WAVs in ordinary editors; preserving image dimensions, atlas layout,
and audio format is safest. `db` is JSON and can be edited as text. The decoded
game/block files are **binary, not JSON or original Fancade source**.

After editing:

```sh
npm run assets:pack
npm run native:build
```

`assets:pack` applies changes to **`webapp/index.data` and the asset-offset
table in `webapp/index.js`**. The table must change if payload sizes change.
All other loader code is preserved. Unchanged assets reuse their original
compressed bytes; changed zlib assets are recompressed. An untouched
unpack/repack is byte-identical. File offsets, total size, compression metadata,
and audio flags are maintained automatically.

Keep filenames and the file list in `manifest.json` intact: adding/removing
assets is not a supported operation. Missing files fail instead of silently
being dropped. Don’t edit `original.data` or `original-loader.js`; they are
hash-checked baselines. To undo a particular asset edit, restore that decoded
file from your own backup and repack. Repeated packs use the same extraction
baseline, so keep editing the unpacked files.

To re-extract, first **move the whole `unpacked/` directory elsewhere** to protect
your edits. The tool refuses to overwrite it.

## 3. Read and modify compiled logic

```sh
npm run tools:install
npm run decompile
```

Output in `modding/generated/`:

| File | Use |
| --- | --- |
| `index.wat` | Valid, editable WebAssembly text (~26 MB) |
| `index.dcmp` | C-like pseudocode for reading, **not compilable C** |
| `index.named.wasm` | Analysis binary with recovered debug names |
| `symbols.json` | 116 import mappings, 83 exports (76 recovered function names), input hashes |
| `strings.json` | Printable strings with **WASM file offsets**, not runtime addresses |
| `loader.pretty.js` | Readable analysis copy of the original loader |

The analysis binary keeps recovered function names but omits generated local
debug names. This works around a WABT decompiler bug that otherwise displays some
parameters as separate, apparently uninitialized local variables. The editable
WAT still retains its convenient local identifiers. Pseudocode is only an aid;
use validated WAT as the authoritative representation.

The binary is stripped. Original local names, classes, comments, and project
structure cannot be recovered. Unidentified functions retain names such as
`$f281`. Names available in the JavaScript loader are restored as WAT identifiers
and in pseudocode; the quoted, minified import/export names stay unchanged so
the original game ABI still matches.

Useful starting points to search in the WAT/pseudocode:

- `get_app_version`: simple constant-return function.
- `app_pause`, `app_resume`: lifecycle entry points.
- `level_select_menu_start_level`: takes one `i32`; use the native SDK adapter,
  which performs the stock world teardown, bounds check and one-reload-per-frame
  guard before calling it.
- `ad_rewarded_on_showed`, `ad_rewarded_on_rewarded`: local reward callbacks.
- `storage_write`: persistence boundary, not a decoded save editor.

### Work on individual functions instead of a 26 MB file

```sh
npm run wasm:split
npm run wasm:inspect -- get_app_version
npm run wasm:inspect -- level_select_menu_start_level
npm run wasm:inspect -- ad_
```

The split step creates `modding/generated/workspace/`:

| Path | Purpose |
| --- | --- |
| `functions/*.wat` | 2,302 individually editable WASM function definitions |
| `pseudocode/*.dcmp` | Corresponding read-only pseudocode snapshots |
| `index.json` | 2,418 functions, including 116 imports: machine signatures, original line ranges, direct callers/callees, exports |
| `REPORT.md` | Linked overview of exported functions, common internal functions, and the full index |

`wasm:inspect` accepts an exact name, a partial-name search, or an absolute
function index such as `"#1400"` (quote the `#` for your shell). For an exact name:

```sh
npm run wasm:inspect -- level_select_menu_start_level --wat
npm run wasm:inspect -- level_select_menu_start_level --pseudocode
npm run wasm:inspect -- level_select_menu_start_level --json
```

These signatures are WASM machine types, not recovered C++ classes or ownership
rules. Call references are static snapshots: indirect calls and function-pointer
targets are unresolved. Changes to function files do not automatically regenerate
their pseudocode or index. A lack of direct callers does not mean code is unused:
tables and callbacks can invoke it.

`wasm:split` refuses to overwrite an existing workspace. Keep `index.json`
untouched. Missing files, renamed function identifiers, injected extra definitions,
and stale baselines fail the build rather than being silently ignored.

### A small, verified patch

Edit `modding/generated/workspace/functions/01401-get_app_version.wat`:

```wat
(func $get_app_version (type $t15) (result i32)
  i32.const 166)
```

Change just `166` to `167`, then:

```sh
npm run wasm:build
npm run native:build
./native/build/drive-mad
```

`wasm:build` validates the WAT and checks import/export names, kinds, order, and
function parameter/result signatures against the original. It is not a proof of
behavioral compatibility or of unchanged memory/table limits and global types.
It writes **`webapp/index.modded.wasm`**, never `index.wasm`. The builder
automatically merges function-file edits into the full module in memory and
reports which functions changed; it does not rewrite `index.wat`.

The native builder consumes that alternate module. The startup line
(`Compiled game version: 167`) confirms the patch. Restore `166` and rebuild
when done experimenting.

### Monolithic editing and protecting work

Without a workspace, the builder still assembles `index.wat` directly. When a
workspace exists, don’t also edit the parent `index.wat`: the builder detects
the conflict instead of choosing a source silently. To deliberately ignore all
function files and build only the monolithic file:

```sh
npm run wasm:build -- --monolithic
```

To re-split, **move the entire `workspace/` directory to a backup first**, then
run `npm run wasm:split`. There is no destructive overwrite option. If you need
to keep function edits across a changed baseline, transfer them deliberately
from the backup into the newly split functions.

Editing `index.dcmp` or `pseudocode/*.dcmp` has no effect; **only WAT gets
reassembled**. Structural WASM validation doesn’t guarantee the game still
works. Gameplay memory/physics operations fail closed after meaningful logic
edits because their offsets are verified against the stock executable. Keep mod
patches in version control or separately backed up: generated files and
extracted assets are intentionally gitignored.

## Verification

```sh
npm test
```

Linux tests cover byte-identical asset roundtrip, changed-size JSON repacking,
real WASM assembly/ABI checks, function splitting/merging, signature and
call-reference analysis, protection against conflicting edits, native
filesystem/save isolation, SDK plugin loading and APIs, real physics changes,
and the workbench. GUI tests need a display and `DRIVE_MAD_TEST_GUI=1`; CI runs
them under Xvfb.
