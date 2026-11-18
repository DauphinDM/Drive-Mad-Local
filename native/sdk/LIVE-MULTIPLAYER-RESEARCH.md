# Live multiplayer: research and feasibility

Research date: **2026-11-18**. Repository baseline: **`3f0870b`**.
Scope: native Drive Mad, **2–4 simultaneous players**, friend race ghosts on
existing maps, and cooperative adaptations of suitable multi-vehicle maps.

**This document is research, not an implementation or a claim that these modes
already work.** No game code, assets, dependencies, hosted resources, firewall
rules, or releases were changed for this research. Companion:
[implementation plan](LIVE-MULTIPLAYER-PLAN.md).

Quick navigation: [conclusions](#1-executive-conclusion) ·
[timing/profile constraints](#5-timing-compatibility-and-integration-constraints) ·
[free hosting/P2P](#7-free-finder-and-native-peer-transport) ·
[web sources](#9-external-source-register) ·
[maps and candidates](#10-shipped-maps-what-was-actually-inspected) ·
[controls/co-op gaps](#11-independent-controls-restart-and-shared-state-gaps) ·
[decisions](#12-research-decision-register) ·
[new binary contracts](#13-newly-inspected-stock-engine-contracts).

## 1. Executive conclusion

The best starting point is the original engine, not replacement vehicle physics
or a second renderer. Existing Versus supplies useful vehicle templates,
opponent creation and recorded-transform playback. It does **not** supply live
rooms, peer connections, arbitrary-level ghosts or cooperative ownership.

Treat the requested experience as two modes sharing transport and lobby code:

1. **Friend race:** each player simulates their own complete original level.
   Stream the visible player-controlled vehicle assemblies to friends as
   noncolliding ghosts. Start together and rank valid completion durations.
   Fade friends as they approach the local player without hiding them entirely.
2. **Cooperative puzzle:** one peer, the host, simulates the authoritative shared
   level and its scripts/physics. Friends supply independently routed controls;
   clients render replicated state. Adapt selected original maps through
   explicit, reviewed variants, not a heuristic that assigns every body a player.

The first is substantially less invasive than the second. “Works on every map”
is an acceptance target requiring a map-by-map coverage ledger, not a capability
established by the current Versus implementation. Likewise, no verified list of
all cooperative candidates can be inferred just from map names or body counts.

A small free-tier signaling service is plausible. A server that only introduces
peers cannot guarantee connectivity across all NATs/firewalls. TURN fallback
relays traffic and therefore is **not finder-only**. Offer direct-only mode with
honest failure messages, or explicitly opt into a bounded relay service.

## 2. Evidence labels and reproducibility

- **Verified in source:** inspected tracked code or exact shipped bytes.
- **Previously observed:** native runtime observations documented by existing
  research; not repeated during this Windows research session.
- **Proposed:** architecture, limits, defaults and APIs to implement and test.
- **Unknown / gate:** reverse-engineering or runtime work required before a
  feature can be promised.

Existing primary repository evidence:

| Reference | What it establishes |
| --- | --- |
| [MULTIPLAYER-RESEARCH.md](MULTIPLAYER-RESEARCH.md) | Original replay transport/format, specific engine functions, prior native observations. |
| [GAMEPLAY-RESEARCH.md](GAMEPLAY-RESEARCH.md) | Researched physics operations, fixed-step evidence, lifecycle and private-profile limits. |
| `native/versus.c`, `tests/native-versus.test.mjs` | Bounded original-service GET bridge; uploads blocked; deferred completions. |
| `native/gameplay.c`, `native/sdk/include/drive_mad_gameplay.h` | Actual supported gameplay controls, not hypothetical player-car APIs. |
| `scripts/lib/native-gameplay.mjs` | Exact-stock executable gate and private-function signature checks. |
| `native/main.c:182–228` | Host loop order and one callback invocation per host frame; no public physics-tick hook. |
| `scripts/build-native.mjs` | WAT-to-C pipeline, generated ABI/profile, native linkage and build hashes. |
| `.github/workflows/build.yml` | Actual platform build and test coverage. |

Stock executable profile: original game v166, SHA-256
`47c6c608201fcfb0b33f8943773b6e61571dd2b17dea39179a8637964453ec66`.
Function indices in existing research are **absolute WASM indices**, not table
slots. Generated pseudocode is analysis material, not compilable original C.

### Local validation limits

`npm test` was attempted on 2026-11-18, using Node 24.14.0 on Windows. Result:
**1 passed, 3 failed, 8 skipped**. The three failures are module-load failures:
the local checkout has no installed `wabt` package. Native integration tests
also require a built native executable; several currently explicitly gate on
Linux. This is an environment baseline, not multiplayer test evidence. No
dependency installation, native build, engine execution or multiplayer trial
was performed as part of this document.

Untracked `dist/` and `src-tauri/` were present before research and left intact.

## 3. Requested product contract

| Requirement | Intended behavior / verification |
| --- | --- |
| Up to four players | One host plus up to three guests; reject a fifth; test every departure/order. |
| Free finder | Invite-code rooms first; optional public discovery later; no default game-state relay through signaling. |
| Peer-to-peer play | Show selected direct/relay status; no claims that all networks support direct connectivity. |
| Existing racing things | Reuse recovered templates, part mapping and transform logic where valid. |
| All existing maps | Every runtime level record accounted for; each playable map must pass coverage before claiming completion. |
| Fastest completion | Approximately synchronized start; monotonic elapsed time plus simulation progress and verified local win event; slowdown/DNF/timeout policy. |
| Visible friends | Correct assembly/vehicle representation, independent camera, stable identity/name/color. |
| More see-through nearby | Presentation-only opacity; never changes collision or cooperative physics. |
| Cooperative multi-car maps | Reviewed role assignment and shared-state adapter per variant, preserving original source map. |
| No work yet | Research/planning documents only; implementation/deployment requires a later execution phase. |

### Recommended initial rules, pending product approval

- Private invite rooms; no accounts, chat, public directory or asset downloads.
- Stock content/profile parity enforced before loading. Trusted native plugins
  remain outside the security boundary; incompatible gameplay mods disallowed.
- Race: same map and rules; no racer-to-racer collision or shared moving props.
  Each player encounters their own original hazards and scripted world.
- Race restart: complete-room synchronized restart initially. Independent
  retries can follow with an explicit attempt counter and noncomparable-run UI.
- Race finish: crossing the original verified win condition, not proximity to
  a guessed finish coordinate. Tie threshold and post-first-finish timeout are
  configurable, initially proposed as a 20 ms tie window and 60 seconds. Record
  physics-tick duration too; expose the chosen scoring clock explicitly.
- Co-op: shared restart vote; loss/win determined by reviewed original-script
  semantics. No host migration in version one: host departure ends the attempt.
- Mid-attempt join: spectator until the next attempt. Co-op reconnect requires
  a valid full baseline; otherwise also waits for restart.
- A map with two meaningful vehicle roles remains a two-role map unless a
  deliberate three/four-player adaptation is validated. Do not force four
  active cars into every original puzzle to satisfy room capacity.

## 4. Why race and co-op need different simulation models

In a ghost race, remote vehicles are presentation. A missed packet can degrade
a friend's visual motion without altering local handling or completion. Each
player's world may diverge intentionally; remote bridges, crates and traps do
not affect the local world. Scripts that draw or switch between multiple local
vehicles require streaming the correct *assembly set*, not merely one chassis.

In co-op, one player's vehicle can push a crate used by another. Car transforms
alone cannot reproduce collisions, joints, motors, switches, spawn/despawn,
timers, win/loss state, scripts or random state. Choose host authority before
adding prediction. First prove a passive client can reproduce the host's visual
world and camera while its local scripts cannot contradict the host.

Do **not** choose cross-platform deterministic lockstep just because the original
program came from WASM: native translations use platform/compiler floating-point
behavior and engine execution contains additional state. Determinism is an
experiment to measure, not a fact supplied by a fixed timestep.

Technical background, consulted via web research:

- [Snapshot interpolation](https://gafferongames.com/post/snapshot_interpolation/):
  buffer timestamped transforms rather than rendering the latest arrival.
- [State synchronization](https://gafferongames.com/post/state_synchronization/):
  a continuing simulation also needs velocities and correction rules.
- [Deterministic lockstep](https://gafferongames.com/post/deterministic_lockstep/):
  identical input is insufficient without exact deterministic simulation.

## 5. Timing, compatibility and integration constraints

### Host frame is not a physics tick

`native/main.c` invokes the game callback once per host loop. Existing research
traces fixed approximately 1/60-second simulation substeps inside `f1402`, with
a smoothed/capped accumulator. A host frame may contain zero or several steps.
The existing frame-step UI explicitly does not promise a single physics tick.

Networking needs an audited hook at actual simulation boundaries for input,
race clock and snapshot capture. Polling after `call_v0` alone is suitable only
for an early visual prototype, not authoritative step scheduling.

Use a monotonic session clock; do not use scaled game date-now or wall-clock UTC
for network deadlines. Existing time scaling affects UI/timers as well as
physics. During scored multiplayer, disable pause/time-scale/force/material
cheats and unsafe gameplay-mod controls, with a visible reason.

### Keep the private-ABI safety boundary

`supportsGameplayProfile()` compares executable module text with the stock
binary; meaningful WAT changes disable the current private profile. Never set
`DM_GAMEPLAY_PROFILE=1` manually to make multiplayer patches run. A new adapter
must either use audited, reproducible native instrumentation of the checked
stock translation, or introduce a separate explicitly pinned instrumented
profile with a patch manifest and regression fixtures. See the plan's profile
gate. Asset parity needs its own hash; stock logic equality does not validate
modified assets or trusted plugins.

### Build/release considerations

The native executable is C11/C++17, SDL2, desktop OpenGL 2.1 on Windows/macOS and
GLES2 on Linux, compiled from WAT with pinned WABT 1.0.39. Networking must not
require a browser, Tauri or Electron runtime. Extend both native linker flags
and recursive runtime-library packaging for any new dependency. Test x64
Windows/Linux and ARM64 macOS; do not infer cross-platform physics agreement.

Existing CI exercises full graphical tests primarily on Linux/Xvfb. Windows
and macOS self-tests are not proof of four-player rendering/input/networking.
The future multiplayer suite must make intentional skips visible and add real
native checks per supported OS.

## 6. Engine, maps and networking findings

The following sections capture parallel research results and distinguish source
evidence from future work.

## 7. Free finder and native peer transport

### Recommended stack

**Proposed:** libdatachannel C API, a Cloudflare Worker, and one SQLite-backed
Durable Object per private room. The finder exchanges membership, SDP and ICE
candidates over verified HTTPS/WSS. Gameplay runs on WebRTC DataChannels between
players, not through the Worker. Keep the original replay GET bridge separate;
do not loosen its allowlist to turn it into arbitrary networking.

```text
                HTTPS/WSS signaling only
  player clients <----------------------> Worker / room Durable Object
         |
         +---- ICE/STUN address discovery + connectivity checks
         |
  guest A <==== encrypted DataChannels ====> host
  guest B <==== encrypted DataChannels ====> host
  guest C <==== encrypted DataChannels ====> host
                  direct if possible
                  optional TURN relay if explicitly enabled
```

Start with a **star**: three peer connections for four players. The host forwards
race snapshots to the other guests, while co-op naturally uses host authority.
A full four-player mesh requires six peer connections; it can reduce ghost
forwarding latency but increases negotiation, exposure and failure paths.
Mesh is an optional later race optimization, not the initial architecture.

The service remains available for reconnect/signaling, but an already-connected
game should survive a temporary finder outage. A signaling room's host is not a
dedicated authoritative game server: game authority resides on a player's PC.

### Hosting comparison, checked 2026-11-18

| Provider | Documented constraints | Assessment |
| --- | --- | --- |
| Cloudflare Workers Free + SQLite DO | Worker 100,000 requests/day and 10 ms CPU/request; DO 100,000 requests/day and 13,000 GB-s/day; SQLite-only DO on Free. | Recommended small private-room finder. |
| Supabase Free Realtime | 200 peak connections, 2 million messages/month, 5 GB egress; free projects may pause after one week inactive. | Viable if auth/database is otherwise needed; extra native protocol integration. |
| Render free web service | 750 free instance-hours/workspace/month; sleeps after 15 minutes without inbound traffic, roughly minute-long wake-up; ephemeral filesystem/restarts. | Conventional WebSocket server, weaker occasional-play experience. |
| Self-hosted server | No guaranteed free public reachability, bandwidth, electricity or maintenance. | Useful only when a suitable existing machine is available. |

Cloudflare DO details matter more than the provider name:

- Use `this.ctx.acceptWebSocket(server)` (Hibernation API), not ordinary
  `server.accept()`; idle hibernatable connections avoid duration billing.
- Restore socket membership from `serializeAttachment` / `deserializeAttachment`
  and necessary persistent room metadata. In-memory fields reset on hibernation.
- Protocol ping/pong does not wake the DO; avoid perpetual application timers.
- Incoming WebSocket messages have a 20:1 **billing** request ratio; this is not
  a promise that all enforcement/metrics count 20 messages as one.
- Free SQLite allowance: 5 million rows read/day, 100,000 written/day, 5 GB total
  storage. Do not store every candidate or packet. Remove expired room data.
- On Free, exhausted allowance causes failures; daily limits reset at 00:00 UTC.
- No free availability/SLA claim is made. A public unauthenticated endpoint can
  be abused into consuming these limits even with only four real friends.

### Direct-only versus relay fallback

ICE (RFC 8445) tests candidate pairs. STUN discovers server-observed address
mappings, not necessarily mappings reachable by the intended peer. It does not
relay packets and cannot guarantee a working direct route. NAT
mapping/filtering, carrier networks, VPNs, address-family mismatch and blocked
UDP can prevent direct play. CGNAT alone neither proves success nor failure.

| Policy | Server role | Honest user expectation |
| --- | --- | --- |
| Direct-only, recommended strict-free default | Finder plus STUN, no gameplay relay | Some network pairs will fail; actionable diagnostics required. |
| Optional automatic TURN | Finder plus authenticated relay fallback | More networks work; traffic/billing/service limits apply. |
| Optional relay-only privacy | All game traffic through TURN | Set relay-only policy before gathering/signaling, filter candidates/diagnostics, recreate previous direct connections; higher relay usage. |

Cloudflare Realtime currently includes **1,000 GB/month shared between SFU and
TURN**, then **$0.05/GB egress**. This is not unlimited free TURN and is separate
from Worker/DO pricing. STUN is documented as free/unlimited. Cloudflare supports
TURN UDP/TCP/TLS, including TLS on port 443; actual backend support must be tested.

Illustrative payload budget: three 1,000-byte host-to-client streams at 20 Hz
are 60,000 bytes/second, **216 MB/hour** (decimal), before inputs/protocol overhead.
Race guest forwarding and more complex co-op snapshots change the calculation.
Actual TURN billing depends on selected allocations and metered traffic paths;
measure instead of assuming a multiplier.

No documented universal TURN hard invoice cap was established. Spending limits
documented for other Cloudflare services must not be represented as a TURN byte
cap. Credential issuance quotas and expiration reduce abuse; issuance quotas
alone do not revoke existing credentials. Cloudflare also documents explicit
credential revocation, but it is not an instantaneous per-byte billing cutoff. Strict
zero-cost operation means disabling hosted TURN unless refusal/billing terms
are separately acceptable. Do not silently enable a paid account or add a card.

### libdatachannel fit and caveats

Verified upstream header/API documentation provides C bindings in `rtc/rtc.h`,
Windows support, peer connections, DataChannels, DTLS, ICE, WebSocket clients,
STUN and TURN. It is a C++ implementation behind C bindings, so dependency/runtime
packaging is required. Build with media disabled (`NO_MEDIA=ON`) if using only
data; retain WebSockets for signaling. Pin a release/commit and test toolchain
compatibility rather than depending on moving `master`.

Important: current API docs restrict TURN TCP/TLS to the **libnice** backend.
Default **libjuice** is simpler for direct/UDP-TURN experiments. Setting
`enableIceTcp=true` does not make libjuice provide TURN/TLS. If UDP-blocked
networks are an acceptance target, evaluate libnice's added GLib/build/runtime
cost and verify TLS/443 in a restrictive-network test. Neither backend proves
reachability through every corporate proxy.

Use real documented APIs: `rtcCreatePeerConnection`,
`rtcSetLocalDescriptionCallback`, `rtcSetLocalCandidateCallback`,
`rtcSetRemoteDescription`, `rtcAddRemoteCandidate`,
`rtcCreateDataChannelEx`, `rtcSendMessage`, `rtcGetBufferedAmount`,
`rtcGetSelectedCandidatePair`, and `rtcCreateWebSocketEx`.
Remote ICE candidates must wait until the remote description is installed.
Never call `rtcCleanup()` from a library callback. Keep TLS and SDP fingerprint
verification enabled.

## 8. Session safety, trust and privacy

- Invite room ID is not sufficient authentication: use an unpredictable invite
  secret, short-lived member credentials and socket-bound identity. Prefer a
  long invitation token; any convenient short code needs rate-limited redemption.
- Validate room capacity, source membership, allowed destination and negotiation
  epoch on the server. Derive `from` from the socket, never client JSON.
- Bind exchanged SDP/fingerprints and candidates to authenticated room membership,
  expected peer IDs, room epoch and connection ID. WSS verifies the service; DTLS
  alone does not identify the intended friend. Reject unexpected channels and
  stale/replaced peer connections.
- Only the host may select map/rules/start/reset and assign co-op ownership;
  each peer may submit inputs/state only for its assigned slot/entities.
- Bound counts, byte lengths, frame rates, tick windows, decompression and queue
  sizes. Reject NaN/infinity, invalid transforms and stale scene generations.
  Never deserialize network bytes into raw native structs or engine memory.
- Native plugins are trusted native code, not sandboxed. Stock parity reduces
  accidental mismatch but does not prevent a malicious peer from lying.
- Locally observed race finish reports provide friend-room competition, not
  trusted global rankings. A host can also cheat; no dedicated adjudicator is
  proposed. Do not upload multiplayer results into stock single-player rankings.
- Direct play exposes public endpoint addresses to peers. Signaling sees SDP
  candidates, potentially including local addresses; relay operators observe
  network metadata. State this in the UI and avoid SDP/IP/token logging by default.
- Keep TURN API tokens server-side and issue short-lived credentials only to
  authenticated members. Do not embed a provider master key in an executable.
- No game assets, arbitrary URLs, native plugins or full replay blobs should be
  accepted from strangers in version one. Use installed content and hashes.

### Licensing and redistribution

Current libdatachannel (since 0.18) is MPL-2.0; older versions used different
terms. Audit the selected version and backend/transitive licenses. MPL is
file-level copyleft: covered source, notices and source-availability obligations
remain relevant to binary distribution, including static linking. Consult the
upstream license/Mozilla FAQ rather than treating this as legal advice.
These library terms grant no rights to redistribute Drive Mad/Fancade assets,
extracted maps or proprietary engine modifications. Preserve map attribution
and review upstream distribution permissions before publishing new packages.

## 9. External source register

Official documentation was preferred over search-result snippets. Provider
prices/limits and moving upstream headers must be rechecked before deployment.

| Source | Relevance |
| --- | --- |
| https://github.com/paullouisageneau/libdatachannel | Platform support, C bindings, license. |
| https://github.com/paullouisageneau/libdatachannel/blob/master/DOC.md | C API contracts, callbacks, ICE backend restrictions. |
| https://github.com/paullouisageneau/libdatachannel/blob/master/include/rtc/rtc.h | Verified config, callback, reliability and DataChannel field names. |
| https://github.com/paullouisageneau/libdatachannel/blob/master/BUILDING.md | Build options and dependencies. |
| https://github.com/paullouisageneau/libjuice | Default lightweight ICE backend. |
| https://developers.cloudflare.com/workers/platform/limits/ | Worker free limits. |
| https://developers.cloudflare.com/durable-objects/platform/pricing/ | DO compute/storage free tier and billing ratio; coordinator fetched directly. |
| https://developers.cloudflare.com/durable-objects/best-practices/websockets/ | Hibernation and attachments; coordinator fetched directly. |
| https://developers.cloudflare.com/realtime/turn/ | Relay endpoints and behavior. |
| https://developers.cloudflare.com/realtime/turn/faq/ | Relay billing/limits. |
| https://developers.cloudflare.com/realtime/sfu/platform/pricing/ | Shared 1,000 GB allowance and egress rate; coordinator fetched directly. |
| https://developers.cloudflare.com/realtime/turn/generate-credentials/ | Server-side temporary credential endpoint. |
| https://developers.cloudflare.com/realtime/turn/generate-credentials/#revoke-credentials | Explicit revocation, not an instantaneous byte-budget guarantee. |
| https://developers.cloudflare.com/billing/manage/spend-management/ | Limits on cost-control assurances. |
| https://supabase.com/pricing | Alternative signaling quotas. |
| https://render.com/docs/free | Alternative hosting sleep and usage limits. |
| https://www.rfc-editor.org/rfc/rfc8445.html | ICE connectivity establishment. |
| https://www.mozilla.org/en-US/MPL/2.0/FAQ/ | MPL obligations. |
| https://github.com/coturn/coturn | Self-hosted TURN alternative, not free infrastructure. |

The three Gaffer references in section 4 explain networking techniques; they do
not establish that this engine exposes the necessary state hooks.

## 10. Shipped maps: what was actually inspected

The content agent inspected loader metadata and decompressed assets **in memory**
using the existing read helpers. It also inspected initialized stock-WASM memory
with inert imports without calling constructors/gameplay. The stock WASM hash
matched the profile above. No map files were extracted to disk.

This checkout lacks `modding/generated/`, its WAT/pseudocode workspace,
`native/build/`, installed `wabt`, and a decompiler on PATH. Engine-function
details in existing research therefore remain previously recovered evidence,
except the direct binary inspections recorded in section 13; none were newly
re-decompiled. This limits what can honestly be concluded.

### Exact package inventory

`readPackage`, `validatePackage`, `loadInputs`, `decodeAsset`, `assetPath` and
`recoverSymbols` in `scripts/lib/modding.mjs` already provide useful validated
package-reading primitives. `readPackage` parses literal metadata without eval;
`decodeAsset` inflates recognized zlib data or returns raw bytes.

The package contains **261 assets**: 14 game binaries, 219 block definitions,
3 PNG atlases, 23 WAVs, database JSON and leaderboard JSON. Game binaries are
custom formats, not recovered Fancade source or JSON.

| Package asset under `/assets/games/` | Decoded bytes | Embedded title |
| --- | ---: | --- |
| `drive_mad_2` | 7,130,023 | Drive Mad |
| `drive_mad_2_construction` | 2,727,761 | Construction |
| `drive_mad_2_cyber` | 5,305,298 | Cyber |
| `drive_mad_2_medieval` | 3,559,234 | Medieval |
| `drive_mad_2_monster_trucks` | 1,990,702 | DM Monster Truck |
| `drive_mad_2_science` | 1,815,402 | Science World |
| `drive_mad_2_space` | 8,396,295 | Dm Space |
| `drive_mad_2_winter` | 11,297,207 | Dev Winter |
| `drive_mad_versus` | 9,765,262 | Dm Versus |
| `dm_vs_cups` | 65,632 | Cups Models |
| `menu` | 375,385 | New Game |
| `menu_dm_dioramas` | 653,393 | Dioramas Monster |
| `menu_dm_medals` | 18,599 | Truck Team |
| `menu_drive_mad` | 1,037,249 | New Game |

Existing gameplay research observed **200 initialized runtime level records**
for the tested content, not an assumed 100 buttons. That does not establish that
each game asset is selectable or that 200 is the union of all themed content.
Inventory must distinguish runtime-playable maps, inaccessible content,
templates, menus, tests and dependencies.

Decoded `/assets/db` is version `v:6`, with fields including `gc`, `gb`, `gw`,
`gf`, `gd` and game-keyed `{l,p}` entries in `w`. Neither `p` nor game keys have
verified player-count semantics. **Do not call `p:2` multiplayer metadata.**

### Name candidates are not a binary decoder

Recurring bytes `18 03 <u8 length> <printable name>` and
`19 03 <u8 length> <printable name>` yielded 200 plausible base-game level names.
Other assets produced obvious false positives (`$Brk`), templates and test names.
Candidate byte order must not become authoritative runtime index mapping.

These base-game names and marker offsets were verified in decoded
`/assets/games/drive_mad_2`; they are **inspection candidates, not certified
multi-car/co-op maps**:

| Candidate | Decoded marker offset | Nearby labels / evidence |
| --- | ---: | --- |
| Double Trouble | 288,507 | `$Offset`, `Failed`, `$Win`, `achvmnt` |
| Control Room | 1,987,233 | Name-based lead only |
| Tricky Switch | 2,004,568 | Name-based lead only |
| Chick Swap | 3,066,506 | `$You`, `$CMan`, `$AutoL`, `$AutoC`, `$Acc`, `$AutoV`, `$CFlyR`, `$CFlyL` |
| Dual Drive | 4,266,180 | `$YsMan`, `$You`, `$Acc`, `$YsAcc`, `$Brk`, `$DieAll` |
| Team Work | 5,509,275 | `$You`, `$YsLose`, `$Win`, `achvmnt` |
| Two High | 5,520,347 | `$Grip`; name-based lead |
| Timesync | 5,744,180 | `DsDir`, `DsLen`, `Stop`, `$You`, `$Offset`, `$Grip` |

Investigate **Dual Drive, Team Work, Double Trouble, Chick Swap** first. Additional
name-based leads: Monster Trucks' `Tug Of War`, `Overkill`, `Angry Trucks`,
`Final Boss`; Science's `Coworkers`, `Little Helper`, `Trust Fall`, `The Sacrifice`;
Medieval's `Jousting`, `The Coup`, `Reconciliation`; base `Car Wars`, `Carception`,
`Matryoshka`, `Save The Chick`. No claim is made that these require multiple cars.

### Existing embedded controller scripts are the primary recovery target

Verified label locations in decoded `drive_mad_2`:

| Label | Decoded byte offset |
| --- | --- |
| `Find Car` | 6,580,710 and 6,580,835 |
| `Car volumes` | 6,581,880 |
| `Lose When Crash` | 6,582,928 |
| `Camera Follow` | 6,583,418 |
| `Slider Script` | 6,660,955 |
| `Chassis` | 6,750,254 |

Recover their enclosing binary records and references before writing a parallel
controller. A string occurrence is not proof a script is instantiated. Repeated
`$You`, `Car Front` or `Car Back` labels are not unique vehicle identities.
The meaning/scope of `$YsMan`, `$YsAcc`, `$YsBrk`, `$CMan`, `$AutoC` remains unknown.

### Shipped blocks establish useful leads, not public native contracts

Package paths under `/assets/blocks/` include object creation/destruction
(`EO_CREATE_EO`, `EO_DESTROY_E`), transform get/set (`EOVQ_SET_POS_E`,
`O_GET_POS_VQ`), velocity (`O_GET_VEL_VV`, `EOVV_SET_VEL_E`), forces/materials,
constraints/motors/springs, collision (`EO_COLLISION_EOFVE`), touch/joystick/button
sensors, `E_LATE_UPDATE_EE`, `FRAME_F`, win/loss (`E_WIN_E`, `E_LOSE_E`),
camera (`EVQF_SET_CAM_E`) and RNG (`EF_RANDOM_SEED_E`, `FF_RANDOM_F`).

Their shipped existence does not prove argument encoding, opcodes, active usage,
ownership or safe snapshotting. Initialized strings `After Physics` (17016),
`Custom Physics` (17030), `Debug Physics` (17045), `Save Data` (45053),
`Load Data` (45063), `Same screen multi-player` and `2-Player` are recovery leads.
Generic Fancade editor strings do not establish an implemented Drive Mad mode.

## 11. Independent controls, restart and shared-state gaps

### Current input is a single global stream

Source inspection found one key array/physical-key array/mouse set and one game
GLFW key callback (`native/host.h`, `native/platform.c::key`).
`platform_drive()` merges A/D (65/68), arrows (263/262) and auto-drive into that
stream. `gameplay_key()` may invert it. `DMInput` has no player/device field;
`mods_input()` is first-consumer-wins, not per-vehicle routing. Held-key releases
are forwarded to avoid stranded acceleration.

**Giving player one A/D and player two arrows does not create two owners.**
Trace input sensors and script-local versus global variables, then route each
role through the existing controller path. Verify that multiple cars are not
intentionally driven by the same controller before changing that behavior.

The shipped Couch Chaos example is **hot-seat**: F7 restarts, F8 swaps/restarts,
F9 manually claims a win, and mod-local storage records scores. It demonstrates
neither simultaneous independent cars nor automatic finish events.

### Restart is whole-level, not per-player respawn

`native/gameplay.c::gameplay_call` validates level count/index, allows only one
reload between completed core frames, calls `game_physics_cleanup` (`f288`),
then the existing `level_select_menu_start_level` path (`f281` → `f387`).
Skipping teardown leaks worlds and overflows the camera backup depth. All
network attempt changes must respect this lifecycle and invalidate stale refs.

No verified safe API restores an individual car's chassis/wheels/constraints,
controller state, teammates' contacts or failure timers. Full-room restart is
the conservative first version. Do not add teleport-only “respawn” and call it
correct cooperative recovery.

### Saves are not world snapshots

`native/filesystem.c` writes opaque original storage blobs atomically and feeds
them back through `game_storage_write`. It does not decode checkpoints or live
physics/script state. Mod state under `<save-dir>/mods/<mod-id>/` is separate.
Saving hot-seat scores is not proof of a complete-world serializer.

The original replay includes a seed, part counts, poses and events/actions
validated by `native/versus.c::replay_valid`; `pose_valid` handles four values per
pose. That replay's ability to display a finished recording does not prove
deterministic input replay or lossless restoration of the running world.

### Co-op feasibility gate

Before promising co-op, prove all of the following on one verified map:

1. Two controller contexts independently drive complete vehicle assemblies.
2. Shared collisions, constraints, motors and switches still run original logic.
3. Win/loss and failure propagation are understood, not guessed from labels.
4. Remote clients render host-authoritative objects, script-driven visuals and
   camera without independently simulating contradictory outcomes.
5. Spawn/despawn and restart do not leave stale references or duplicated worlds.

If passive world presentation cannot be separated from stock local scripts,
that is an engine-architecture blocker, not something WebRTC can repair. Reopen
the design rather than shipping cars-only replication as cooperative physics.

## 12. Research decision register

| Decision | Recommendation | Revisit when |
| --- | --- | --- |
| Finder | Private Cloudflare Worker/SQLite DO rooms | Provider terms/quotas change or user chooses another account/provider. |
| Strict-free connectivity | Direct-only default; explicit optional relay | User approves relay terms and an acceptable operational budget/policy. |
| Native transport | Pinned libdatachannel data-only C API | libnice TLS/443 footprint or packaging proves unacceptable. |
| Topology | Host-centered star | Measurements justify race-only mesh. |
| Race simulation | Independent original worlds + noncolliding friend assemblies | User explicitly requests interacting race worlds. |
| Co-op simulation | Host authority, passive remote presentation first | Verified full-state/prediction hooks justify added complexity. |
| Map selection | Content hashes and authoritative runtime/record mapping | Decoder/runtime coverage is complete. |
| Co-op extraction | Additive reviewed variants with source/dependency provenance | Rights and format are sufficiently recovered to package them. |
| Host loss | End attempt; no migration initially | Complete authoritative state transfer is implemented/tested. |
| Race integrity | Friend-room self-reported local outcomes | A trusted referee is requested; finder-only cannot provide one. |
| Four players | Four simultaneous racers; 2–4 co-op roles only where validated | Specific maps receive tested four-role adaptations. |

### Questions to settle before shipping, not blockers to this research

- Is occasional direct-only connection failure acceptable, or is relay fallback
  required? Can a provider account accept any potential overage?
- Does “all existing maps” include every shipped themed asset, or every map
  exposed by the current 200-record runtime menu? Track both until clarified.
- Must every co-op map support four active drivers, or can some have two roles
  plus spectators? Four-player support on each puzzle can require level redesign.
- Should failure require a shared restart, or is individual respawn essential?
- Private friend rooms only, or eventual public discovery/mod support? The latter
  substantially expands abuse, compatibility and moderation scope.

## 13. Newly inspected stock engine contracts

The engine agent independently decoded stock WASM instructions in memory and
inspected initialized shader/string data with inert imports, without running
gameplay. Findings below are **binary/source evidence**, not native runtime
validation. All offsets require the exact stock hash in section 2. Binary ranges
are hexadecimal **file offsets**; decimal data addresses/struct fields are
linear-memory contracts, not generally safe public APIs. Reproduce them through
generated WAT/decompilation and regression fixtures before implementation.

### 13.1 Existing Versus construction and part enumeration

| Function / binary range | Newly checked behavior |
| --- | --- |
| `f913`, `0xB7842–0xB7E20` | Vehicle definition at `5061200 + vehicle_index * 264`; creates chassis/wheels through `f320`; enumerates parts. |
| `f435`, `0x5849D–0x584AA` | Constructs local descriptor at `23226844`. |
| `f435`, `0x584ED–0x58532` | Opponent descriptor `23226912 + index * 60`, recording pointer from `23226788 + index * 4`, replay vehicle identity `+36`, bound at `23224712`. |
| `f1402`, `0x13AD95–0x13B199` | Reads opponent part arrays and applies 16-byte poses at clamped replay tick. |

Verified descriptor fields within this **Versus-only** contract: chassis object
index `+8`, wheel indices beginning `+12`, unified part-index array `+32`, count
`+56`. The local flag is determined by comparison with the local descriptor.
Three remote slots are consistent with prior runtime evidence; do not extend
these arrays just because a larger room would be convenient.

Replay fields inspected: frame count `+60`, part count `+72`, pose storage `+76`,
pose bounds `+80`; poses contain three positions and one angle. This is not a
full generic 3-D vehicle/world format. The current legacy validator's six-part
limit is a defensive replay limit, not a proven all-map assembly maximum.

At `0x13ADD0–0x13ADF1`, playback constructs an opponent offset
`(0, 0, signed_base_Z + 3 * (opponent_index + 1))`. Audit both playback and
initialization when removing lanes; do not change an unexplained constant.

### 13.2 Transform synchronization already exists

Scene-object array pointer `26417668`, stride 392 bytes; scene position is at
`+8/+12/+16`, quaternion at `+32/+36/+40/+44` in the inspected path.

- `f277`, binary `0x2150F–0x2160D`: resolves body through the body table at
  `32434048`, preserves orientation, changes origin, increments body version
  `+284`, copies transform and invokes the motion-state virtual callback via
  body `+504`, vtable `+12`.
- `f459`, binary `0x5C75F–0x5C904`: builds basis from a quaternion, preserves
  origin, increments the version and invokes the same synchronization route.

Prefer auditing/binding these existing paths to inventing raw transform writes.
Neither is currently a supported SDK teleport API, nor does their existence
alone prove every broadphase/collision/presentation consequence is safe.

### 13.3 Opponent motion restrictions do not prove collision exclusion

`f320`, `0x2DF70–0x2E059`, allocates through `f539`, copies a 392-byte scene
object, sets object byte `+94=2`, adjusts position and calls `f665` to create a
body. A body/world branch uses body `+188` and world vtable `+84`. Local copies
take `f538`; opponents call `f1321` and `f1320` with the verified zero vector at
initialized address `192740`. More setup depends on object byte `+92`.

- `f1321`, `0x11949D–0x119516`, writes linear factors at body `+372/+376/+380`
  and inverse-mass-scaled factors.
- `f1320`, `0x119450–0x11949C`, writes body `+568/+572/+576` and version `+284`.

These explain transform-driven opponents, **not** noncollision. An immovable
body can still change another car's contacts. Recover actual collision groups,
masks/contact response or use verified render-only clones. No Bullet flag offset
is approved by this research.

### 13.4 Exact solver call and tick ordering

`f1402` body spans `0x123333–0x13F5F8`. Its fixed-step loop spans
`0x123B77–0x13BEEF`; `0x123B79–0x123B87` compares the accumulator with 1/60,
`0x13BEB2–0x13BECC` subtracts 1/60 from accumulator `8553416`, and
`0x13BECF–0x13BEDF` increments per-callback step count `32463268`.
The loop contains UI/nonphysics work too; not every iteration proves an active
world solver call.

At `0x13A27E–0x13A2A0`, the world at `32434056` calls vtable byte offset 52
(slot 13), WASM type index **142**, with exact signature:

```text
(i32 world, f32 elapsed, i32 max_substeps, f32 fixed_step) -> i32
arguments: world, 0.0166666675, 0, 0.0166666675
return value dropped
```

Replay processing follows that solver call. `0x13B24E–0x13B250` then calls
`f667(1)` (previously recovered After Physics scripting phase), followed by
world-active and internal-state checks (`10341532 == 1`, `10341469 == 0`),
then active gameplay counter `32463272` increment
at `0x13B283–0x13B293`.

The second flag's complete semantics are not recovered; do not treat it as host
pause or a ready-made multiplayer start gate. The enclosing loop continues to
`0x13BEEF`, so the counter is an ordering landmark, not a verified complete-tick
boundary. Inspect the remaining loop body and all exits.

**An after-solver callback is not a complete-gameplay-tick callback.** Snapshots
must account for replay application, After Physics scripts and lifecycle changes.

### 13.5 Native interception feasibility without bypassing stock checks

`native/gameplay.c` already validates world vtable/funcref signatures for gravity.
A proposed stock-native experiment can intercept the exact solver funcref,
preserve its function/module-instance pair, call through a typed trampoline,
and restore safely. This leaves stock WASM/profile equality unchanged.

That experiment has not been compiled/run. It must verify table bounds, exact
signature, instance/world match, shared-table callers, recursion and teardown.
It gives before/after **solver** observations only. Full tick/render hooks still
need audited instrumentation with their own explicit profile if WAT changes.

Adding WASM imports shifts absolute function indices. Appending helpers avoids
that particular shift but still changes declaration counts and executable hash.
New profiles need explicit named adapter exports or profile-specific binding
metadata; stock ordinal bindings must not silently carry over.

### 13.6 Critical transparency finding: inspected lit shader is opaque

The initialized lit 3-D fragment shader string at address **116620** contains:

```glsl
uniform vec3 tint;
/* ... */
FRAGCOLOR.rgb = tint * (light * rgb1 + (1.0 - light) * rgb2);
FRAGCOLOR.a = 1.0;
```

Tint is RGB only. UI/textured shader strings at 115896/116075 support RGBA, but
that does not establish transparency support for vehicle meshes. **Any vehicle
submissions using this shader cannot be faded by ordinary source-alpha blending
alone.** Confirm the program-to-vehicle mapping first, then audit a per-ghost
opacity path. This inspection alone does not establish which program variants
are used by every car draw.

`native/graphics.c` exposes GL bridges including `dm_glShaderSource`,
`dm_glUniformMatrix4fv`, `dm_glDrawArrays`, `dm_glUseProgram`, `dm_glUniform1f`,
`dm_glDepthMask`, `dm_glBlendFunc`, `dm_glDepthFunc`. They lack object identity.
Do not classify a draw as a player car using draw count, a matrix or VBO alone.

Newly checked mesh helper `f526`, binary `0x65860–0x6590C`, receives six i32
arguments, uploads matrices/RGB tint, binds a VBO, configures three 3-float
attributes at stride 36 and calls `glDrawArrays`. Checked callers include `f362`
at `0x3ED9C`/`0x3EEB8`, `f596` at `0x6FE6E`, and `f873` at
`0xB2711`/`0xB2888`. These are leads for existing renderer reuse; per-object
ownership, batching, shadows and pass ordering are not yet recovered.

Recommended proof: positively map ghost part submissions, patch only the exact
matched 3-D shader variant, supply opacity, preserve terrain/local opaque passes,
then draw depth-tested ghosts with depth writes off and restore all GL state.
Audit shadows/depth passes independently and test three overlapping assemblies.

## 14. Remaining experiments in dependency order

1. Reproduce binary contracts with generated WAT/pseudocode and checked fixtures.
2. Stock solver trampoline: exact type, calls per host frame and reload lifetimes.
3. Full gameplay-tick boundary: After Physics, early exits, pause, fail/win/reload.
4. Local Versus part manifests across every selectable existing vehicle.
5. One live-driven clone through original construction/synchronization paths,
   without recorded playback overwriting it.
6. Proven collision/sensor isolation, then three clones and repeated teardown.
7. Object-aware original render mapping, shader opacity and shadows across GL APIs.
8. Regular-map owner/assembly discovery, changes of topology and full orientation.
9. Authoritative map inventory and independent multi-car controller proof.
10. Shared-world passive co-op replica before prediction or map redesign.

The public SDK currently provides host-frame lifecycle/input/render hooks,
typed calls, asset reads and local messages—not solver ticks, ghost creation,
object-aware rendering, collision filtering or live sessions. `publish()` is a
bounded **in-process** cross-mod queue, not networking. Changing SDK structs
requires ABI versioning; the current host checks struct sizes.
