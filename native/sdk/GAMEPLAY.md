# Native gameplay API and workbench

Launch `./native/build/drive-mad --menu`, or press **F2** in the game.
Escape closes the workbench unless a text editor is active. Mouse, wheel,
keyboard, clipboard and text editing use native SDL/Dear ImGui; no browser.
The game keeps running unless you pause it. Opening the workbench releases held
game keys/buttons and captures input before plugins/gameplay. Auto drive is
suspended while the workbench is open.

## Gameplay alterations

The **Gameplay** tab provides:

- Game-clock scale from 0.05× to 3×, pause/resume and a queued host-frame step.
- Auto drive forward/reverse, inverted A/D and arrow controls, reset controls.
- Runtime level count/current index, bounded level selection, restart and next.
- World gravity vectors, zero/Moon/reversed/sideways presets, restore gravity.
- Rigid-body selection, position, linear/angular velocity and inverse mass.
- Set velocity/spin, accumulate force, change friction and restitution.
- Stop all dynamic motion and add vertical velocity to all dynamic bodies.
- Existing local reward callbacks.

**World tools affect the whole physics world, not an identified player car.**
There is no invented vehicle selector, teleport, infinite-health or instant-win
API. Velocity/force operations reject static bodies; material changes can apply
to static bodies too. These are gameplay alterations, not guarantees that every
level remains completable. Level selection/rewards can persist in native saves.

The game clock also drives UI/timers/game timestamps. It is not a physics-only
time scale and does not change the OS clock or host/plugin delta time. A queued
step runs one host frame while paused; the game's smoothed fixed-step
accumulator means this is **not guaranteed to be exactly one physics tick**.
Paused rendering uses a clean workbench backdrop.

## ABI-1 plugin access

`DMApi`/`DMMod` binary layouts are unchanged. Enumerate `export_count/export_at`;
do not hardcode a count. There are currently 76 recovered core exports plus
40 `native.*` entries. All calls use the existing exact-typed `api->call`.
The optional header `drive_mad_gameplay.h` adds convenient C/C++ wrappers:

```c
#include <drive_mad_gameplay.h>
/* From on_ready or later; check every returned status. */
DMStatus slow = dm_game_scale(api, ctx, 0.5f);
DMStatus moon = dm_game_gravity(api, ctx, 0, -1.62f, 0);
/* Body indices come from the current world; do not retain them across reloads. */
DMStatus push = dm_game_velocity(api, ctx, body_index, 0, 4, 0);
```

Names below have the `native.` prefix. `i32` is transported as `uint32_t` bits;
use `(uint32_t)-1` for signed reverse direction. `f32` inputs must be finite.
All calls except `profile_supported` require gameplay readiness.

| Entry | Arguments → result | Meaning/limits |
| --- | --- | --- |
| `profile_supported` | `() → i32` | Early query: known executable layout enabled (1/0). |
| `time_scale` / `get_time_scale` | `(f32) → void` / `() → f32` | 0.05–3. |
| `pause` | `(i32) → void` | 0 resumes, 1 pauses. |
| `step` | `() → void` | Requires pause; queues at most 60 host frames. |
| `auto_drive` / `get_auto_drive` | `(i32) → void` / `() → i32` | -1 reverse, 0 off, 1 forward. |
| `invert_controls` / `get_invert_controls` | `(i32) → void` / `() → i32` | Boolean 0/1. |
| `reset_controls` | `() → void` | Normal clock, drive off, inversion off, held keys released. |
| `level_count` / `level_index` | `() → i32` | Runtime level records/current zero-based index. |
| `start_level` | `(i32) → void` | Must be below the actual current level count. |
| `restart` / `next_level` | `() → void` | Uses recovered level-start export; no wrap at last level. |
| `gravity` | `(f32 x, f32 y, f32 z) → void` | Each component within ±100; persistent override. |
| `gravity_reset` | `() → void` | Restores captured pre-override vector; disables enforcement. |
| `gravity_x/y/z` | `() → f32` | Actual current world vector components. |
| `body_count` | `() → i32` | Current body table length; indices are transient. |
| `body_x/y/z` | `(i32 body) → f32` | Current transform position; read-only. |
| `body_vx/vy/vz` | `(i32 body) → f32` | Current linear velocity. |
| `body_wx/wy/wz` | `(i32 body) → f32` | Current angular velocity, radians/second. |
| `body_inverse_mass` | `(i32 body) → f32` | Positive means dynamic; zero generally static/kinematic. |
| `body_velocity` | `(i32 body, f32 x, f32 y, f32 z) → void` | Set linear velocity, components ±200. |
| `body_spin` | `(i32 body, f32 x, f32 y, f32 z) → void` | Set angular velocity, radians/second, components ±200. |
| `body_force` | `(i32 body, f32 x, f32 y, f32 z) → void` | Accumulate force × body's linear factors; components ±200, **not** an impulse. |
| `body_friction` / `body_friction_value` | `(i32, f32) → void` / `(i32) → f32` | Material coefficient 0–10. |
| `body_bounce` / `body_bounce_value` | `(i32, f32) → void` / `(i32) → f32` | Restitution 0–1. |
| `stop_motion` | `() → void` | Clear dynamic linear/angular velocity and force/torque accumulators. |
| `world_lift` | `(f32) → void` | Add to Y velocity of all dynamic bodies, ±30. |

Level/physics operations require the verified layout profile. Unsupported
profiles return `DM_EPROTECTED`; host clock/input functions still work.
Level reloads are limited to one between completed core frames: another
`start_level`/`restart`/`next_level` returns `DM_EFULL` until a frame runs
(resume or step if paused). The native adapter also tears down the previous
physics world through the researched stock routine before starting a level:
the web export assumes its level-selection UI has already done that cleanup.
The original named `level_select_menu_start_level` SDK call is routed through
the same checked adapter; it is not an unchecked back door to the web export.
Errors do not silently claim success. The typed API browser displays argument
types, result types, flags, call result and status. Host-owned core calls stay
blocked. Other low-level core calls require advanced opt-in because machine
types alone do not reveal pointer ownership or semantic constraints.

## The other workbench tabs

Every public `DMApi` function has a real use in the menu:

| Tab | API operations |
| --- | --- |
| API | Enumerate count/entries; exact-typed call editor for i32/i64/f32/f64; results and protected flags. |
| Assets | Enumerate known paths; size-query/read effective packed bytes; hex/ASCII preview; copy raw preview to own storage. |
| Memory | Allocate and zero owned blocks, list/release owned allocations, bounded reads, hex writes. Default writes require an owned range; arbitrary writes require two explicit advanced controls. |
| Storage | Size-query/read settings; atomic text/blob writes, size-query/read binary preview, confirmed removal in the menu's isolated namespace. |
| Messages | Publish topic/payload and inspect queued messages received from plugins and the menu. |
| Mods / Console | List enabled/disabled mods, send plugin commands, execute the built-in non-shell dispatcher. |
| Diagnostics | State snapshots/counters/readiness, window title, SDK logging/status lookup and input-capture history. |

Memory/asset previews show at most 2 KiB, but display the full byte count.
Guest reads are limited to 2 KiB per UI request; menu allocations are capped at
1 MiB each/64 blocks and released on shutdown. Native API limits remain as
documented in README. The menu owns `mods/native-menu/` storage and reserves
that mod ID. It cannot impersonate another plugin's private settings/storage.
Builtin settings `menu_key` and `profile` are informational snapshots.

The menu does not pretend to hot-load/unload `.so` files, edit all binary
formats, provide a plugin sandbox or supply an undocumented car/health API.
Lifecycle hooks, input consumption and GLES render hooks remain plugin
callbacks; the menu shows state/input/logs but cannot replace your callback code.

## Console examples

```text
api native.profile_supported
api native.time_scale 0.5
api native.gravity 0 -1.62 0
api native.gravity_reset
api native.start_level 3
api native.body_velocity 0 0 4 0
menu api
menu assets
menu memory
menu close
```

`menu` also accepts `gameplay`, `storage`, `messages`, `mods`, `diagnostics`,
`open` and `close`. For example:

```bash
./native/build/drive-mad --menu --command "api native.time_scale 0.5"
./native/build/drive-mad --command "menu assets"
```

Startup commands run after readiness. `api` accepts exactly the declared numeric
arguments; it is not C, JavaScript or shell evaluation. Core pointer-bearing
calls remain advanced/trusted operations even when their numeric types match.

## Compatibility and provenance

The build enables memory-profile operations only if the original WASM SHA256
matches the researched v166 file **and** deterministic, debug-name-free WABT
module text (including data/initializers) of the compiled editable WAT equals
the original. Any executable edit disables the profile, even if apparently
harmless. Renamed debug symbols alone do not. Asset-only mods can still alter
game content/physics; the profile does not validate your custom asset semantics.

See [GAMEPLAY-RESEARCH.md](GAMEPLAY-RESEARCH.md) for exact evidence and the
reproducible `npm run native:research` report. Trusted plugins can invalidate
layout assumptions through arbitrary memory writes; there is no process sandbox.
Back up native saves before experimenting.
