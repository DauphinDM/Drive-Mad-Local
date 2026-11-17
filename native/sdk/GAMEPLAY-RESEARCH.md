# Gameplay reverse-engineering evidence

Profile: original `webapp/index.wasm`, game version 166, SHA256
`47c6c608201fcfb0b33f8943773b6e61571dd2b17dea39179a8637964453ec66`.
Names `fNNN` below are absolute WASM function indices, not indirect-table slots.
The existing decompilation workspace has both exact editable WAT and read-only
C-like pseudocode for each function.

Reproduce string/reference discovery:

```bash
npm run native:research
npm run wasm:inspect -- '$f667' --pseudocode
npm run wasm:inspect -- '$f131' --pseudocode
npm run wasm:inspect -- '$f797' --pseudocode
npm run wasm:inspect -- level_select_menu_start_level --pseudocode
```

`modding/generated/gameplay-research.json` contains initialized-memory string
addresses and referring pseudocode files. The research script instantiates
original WASM with stub imports to inspect initialized data; it does **not**
call its constructors/main or execute the game with fake platform services.

| Evidence | Recovered operation |
| --- | --- |
| `"Set Gravity got invalid"` at 107395, referenced by f667 | Evaluates/validates finite vector; world pointer at u32 address 32434056. World vtable slot 19 gets gravity `(out, world)`, slot 18 sets `(world, vector)`, both `void(i32,i32)`. |
| f435 world creation + f667 gravity branch | Same world storage and virtual setter; validates independent of error-string guess. |
| f667 velocity branch; f131 getters | Linear velocity at body+336/+340/+344, angular at +352/+356/+360; zero SIMD padding, increment u32 at +284, activate body. f131 converts angular velocity to degrees for its scripting operation, proving native storage is radians. |
| f667 Add Force branch | Adds input × per-axis linear factors (+372/+376/+380) to accumulated force (+436/+440/+444); wakes body. This is not a velocity write or instantaneous impulse. |
| f667 material opcodes 332 and bounce branch | Friction at body+224, restitution +228; f797 defaults agree (0.5 friction, zero restitution). |
| f797 constructor (640-byte body) | Inverse mass at +368, force/torque accumulators at +436 and +452; transform origin +52/+56/+60. |
| f665 body creation and f667 bounds checks | Body pointer array at u32 address 32434048; count at 26417664; object limit 16384. |
| f233 activation helper | If `(activation_state & ~2) != 4`, set state at +216 to 1; clear deactivation time at +220. Used after our supported body writes. |
| exported level_select_menu_start_level → f281(level,1,1) → f387 | Existing reload path, not a guessed restart instruction. Current level index at 21361236. |
| app_init + f281/f387 level selection | Runtime count at 10320272, 40-byte records from 10320288. Actual initialized game exposes 200 records in the tested content, not an assumed 100 playable buttons. |
| main installs indirect-table callback 481, whose target is f1402 | Host frame pipeline. f1402 accumulates smoothed elapsed time with capped accumulator and fixed ~1/60 s steps. Table slot 481 is **not function f481**. |
| f196/f244/f1064 + Emscripten date import | Game time comes through native host date-now import; scaling can therefore affect timers/UI/timestamps, not only physics. |
| f288 world teardown, f435 world initialization, f831/f830 camera store/restore | World-active byte 10341532; f288 restores the camera, destroys old rigid bodies/world, and clears that flag. f435 stores a camera and creates physics. Merely calling the web level-start export repeatedly leaks worlds and overflows backup count 38958312 (max 3). Native level controls call the real stock teardown before the existing export and restrict repeated reloads within a completed core frame (`DM_EFULL`). |

The recovered `app_pause`/`app_resume` exports toggle core/audio state, but do not
call Emscripten's host-loop pause imports. Native pause wrappers therefore
explicitly synchronize the host scheduler/state too; both original named SDK
calls and `native.pause` use that adapter. Tests exercise pause, single host-frame
step and resume through the real UI and a separately compiled ABI-1 plugin.

Runtime checks verify memory spans, count/index bounds, finite/ranged input,
positive inverse mass for velocity/force changes, and exact indirect-call
signature before gravity access. The build-time executable equality check
fails closed for modified/unknown logic; it does not assert that arbitrary
data edits or raw-memory plugins preserve invariants.

We did not identify an authoritative player-car pointer, health variable,
win-state setter, spawn/despawn contract or complete game/block serializer.
Consequently those are not exposed as fabricated “safe” APIs. See GAMEPLAY.md
for actual supported controls, units, limits and the whole-world warning.
