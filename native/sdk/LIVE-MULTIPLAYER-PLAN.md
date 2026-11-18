# Live multiplayer implementation plan

Date: **2026-11-18**. Baseline: `3f0870b`. Status: **proposal only**.
Read [the research](LIVE-MULTIPLAYER-RESEARCH.md) and
[existing engine evidence](MULTIPLAYER-RESEARCH.md) first.

This plan is intentionally gated: a successful four-player transport demo does
not prove arbitrary-map racing, and successful ghosts do not prove co-op.
No source changes or deployment described below have been executed.

Quick navigation: [phases/gates](#1-delivery-sequence-and-gono-go-gates) ·
[parallel agent ownership](#2-agentic-execution-workflow) ·
[profile/solver probe](#4-reproducible-baseline-and-profile-gate) ·
[rooms/start](#5-room-compatibility-and-attempt-state-machines) ·
[transport examples](#6-actual-transport-api-example) ·
[wire format](#7-wire-format-parsing-and-resource-budgets) ·
[map extraction](#8-map-inventory-extraction-and-role-discovery) ·
[race/fade](#9-friend-racing-vertical-slice-and-all-map-expansion) ·
[co-op](#10-host-authoritative-co-op-implementation-spike) ·
[Discord Rich Presence](#111-discord-rpc--rich-presence) ·
[verification](#12-verification-plan-and-definition-of-done).

## 1. Delivery sequence and go/no-go gates

| Phase | Deliverable | Gate before continuing |
| --- | --- | --- |
| 0 | Reproducible baseline, isolated saves, source/asset/profile hashes | Build and existing tests pass; failures/skips recorded on each platform. |
| 1 | Read-only level inventory, vehicle/role and finish tracing | Every runtime level accounted for; at least one verified multi-car candidate, not guessed names. |
| 2 | Audited engine adapter, exact tick hooks, lifecycle and ghost opacity proof | One local + three synthetic ghosts, collision isolation, reload stress, no ordinary-map scripts damaged. |
| 3 | Offline loopback transport/protocol and four-process LAN demo | Ownership, sequencing, stale epochs, malformed packets and disconnect tests pass. |
| 4 | Private signaling service source + P2P ICE integration | Local signaling test passes; remote deployment separately authorized; direct/failure/relay policy visible. |
| 5 | Four-player friend racing vertical slice | Same map/vehicle, synchronized start, true win events, correct ranking, three nearby ghosts. |
| 6 | All-map race coverage | Every supported playable map/assembly/switch/restart path tested; remaining exceptions block “all maps” claim. |
| 7 | One host-authoritative co-op vertical slice | Independent role controls, shared puzzle interactions, authoritative scripts, remote camera and restart proven. |
| 8 | Reviewed multi-car co-op collection | Every included variant has provenance, roles, player bounds, completion and failure tests. |
| 8a (parallel after phase 5) | Optional Discord RPC / Rich Presence, then room invites | Presence works without mandatory Discord login; invites use existing authenticated room flow; missing Discord never blocks play. |
| 9 | Hardening, cross-platform play and packaging | Network impairment, security/fuzzing, frame-time/memory budgets and distributable dependency audit pass. |

No credible calendar estimate exists until phases 1–2 and the co-op feasibility
spike finish. Co-op world/script replication is the largest unknown, not room UI.

## 2. Agentic execution workflow

The user explicitly requests parallel agents for research **and later coding**.
Use bounded tasks and independent file ownership, not agents editing the same
engine function simultaneously. A coordinator integrates and reviews all work.

### Research allocation used for this proposal

- Engine agent: original Versus, transforms, controls, render/opacity hooks and
  profile-compatible instrumentation.
- Content agent: map inventory, multi-car detection, cooperative role/physics
  feasibility and extraction provenance.
- Networking agent: official hosting/free-tier documentation, native WebRTC,
  ICE/STUN/TURN limits, security and four-player topology.
- Coordinator: requirements, build/test baseline, integration architecture,
  dependency graph, acceptance criteria and final source verification.

### Later implementation allocation

| Work package | Owned paths (proposed, not present) | Depends on |
| --- | --- | --- |
| Engine adapter | `native/multiplayer/engine.*`, generated binding/profile tooling | Verified contracts from phases 1–2 |
| Protocol/tests | `native/multiplayer/protocol.*`, `tests/multiplayer-protocol*` | Versioned wire schema |
| Transport | `native/multiplayer/transport.*`, native dependency integration | Protocol interface, build agreement |
| Signaling | `services/matchmaker/**`, its own local tests | Room/auth schema, explicit hosting policy |
| Map tooling | `scripts/research-maps.mjs`, reviewed map catalog | Read-only parser contracts |
| Race mode | `native/multiplayer/race.*`, race test fixtures | Tick adapter, ghosts, transport |
| Co-op mode | `native/multiplayer/coop.*`, reviewed variants | Role hooks and shared-world proof |
| UI | `native/menu.cpp` or dedicated multiplayer UI | State machine/API agreed first |
| Discord integration | `native/integrations/discord_presence.*`, optional dependency/package tests | Stable session events, privacy settings and authenticated join flow; coordinate shared build changes |
| Verification | Multiclient harness, impairment/fuzz tests, platform CI | Stable coordinator-owned interfaces |

The coordinator alone edits shared `native/host.h`, main-loop wiring,
`scripts/build-native.mjs` and CI after agents submit narrow patches. Assign a
second agent to adversarial review of parsers, engine lifetimes and auth, not
just feature work. Require review evidence and tests before merging each slice.
No agent may deploy a service, create billable resources, publish a release,
force a profile guard or overwrite unrelated work without authorization.

## 3. Proposed boundaries and API examples

**All following snippets are design examples, not current SDK APIs or tested
implementations.** They deliberately avoid private offsets and guessed car IDs.
Recovered engine details belong behind one checked adapter.

```c
/* Proposed native/multiplayer/engine.h; contract only. */
typedef struct {
  uint32_t local_scene_generation; /* adapter-only; changes on local rebuild */
  uint32_t entity_id;     /* session ID, never a WASM pointer */
  uint16_t generation;    /* reject recycled entity IDs */
} MpEntityRef;

typedef struct {
  uint8_t player_slot;    /* 0..3 */
  uint8_t held_actions;   /* verified game actions, not SDL scancodes */
  uint32_t attempt_id;
  uint32_t tick;
  uint32_t input_seq;
} MpInput;

/* These operations must be researched and implemented, not assumed present. */
bool mp_engine_begin_attempt(const MpMapConfig *config);
bool mp_engine_capture_owned_parts(uint8_t slot, MpAssemblyFrame *out);
bool mp_engine_apply_role_input(const MpRoleBinding *role, const MpInput *in);
bool mp_engine_create_ghost(const MpAssemblyDescriptor *desc, MpGhost *out);
bool mp_engine_present_ghost(MpGhost *ghost, const MpAssemblyFrame *frame);
bool mp_engine_read_outcome(MpOutcome *out);
void mp_engine_clear_scene(uint32_t old_local_generation);
```

Network callbacks must never access translated engine memory, GL or SDL state.
They copy bounded messages into a queue. The main thread validates/dequeues;
verified simulation hooks consume inputs and capture results. Shutdown stops
callbacks, drains/cancels queues, closes peers, removes ghosts, then frees the
engine. A copied pointer is not a lifetime guarantee.

### Proposed hook order

```text
host loop:
  SDL events -> networking event queue -> lobby/control state
  invoke original game callback
    audited fixed-step-begin hook, before controller scripts read input:
      validate scene/attempt -> apply role inputs -> assign tick
    original scripts + original physics, in their verified order
    audited gameplay-tick-complete hook, after remaining per-step work:
      capture owned assemblies / host world changes / finish event
    audited before-scene-render hook:
      present buffered remote ghosts or passive replicated co-op world
  native multiplayer overlay -> swap
```

The exact positions relative to script execution and camera update must be
proven. The existing after-game-drawing `DMMod.on_render` callback is neither a
physics-tick hook nor a pre-scene-render presentation hook; it can run on paused
host iterations too. For ghosts, presentation must occur before the engine draws
the affected objects; changing transforms after its draw is one frame too late.

## 4. Reproducible baseline and profile gate

Commands for the **later execution phase**, not performed during research:

```powershell
npm ci
npm run tools:install
# Only when generated workspace is absent; never overwrite user WAT edits.
npm run decompile
npm run native:build
$env:DRIVE_MAD_TEST_GUI = "1"
npm test
```

Use the documented MSYS2 compiler/dependency environment on Windows; PowerShell
alone does not supply SDL/curl/GL or a compiler. On Linux follow existing CI's
Xvfb/software rendering setup. Record skips rather than calling a skipped GUI
suite a passing native test.

For a new instrumented profile, define a reproducible source manifest:

```json
{
  "profileId": "v166-mp-instrumented-1",
  "upstreamExecutableSha256": "47c6c608201fcfb0b33f8943773b6e61571dd2b17dea39179a8637964453ec66",
  "patchSetSha256": "<computed-from-reviewed-patch-inputs>",
  "instrumentedExecutableSha256": "<computed-after-reproducible-build>",
  "adapterAbi": 1,
  "supportedFeatures": ["tick-hooks", "ghost-presentation"]
}
```

This is a proposed new mechanism, not a valid current profile. Keep the stock
profile as-is. Validate exact target instruction/signature patterns; abort on
zero/multiple matches. Never treat a mere asset hash match as engine-layout
validation. Extend `tests/native-gameplay.test.mjs` so an unreviewed logic edit
still fails closed and the intentionally instrumented build is accepted only
by its separate audited profile.

### 4.1 Stock-native solver probe before WAT instrumentation

New binary evidence in research section 13 identifies a world-step indirect
call in `f1402` through world-vtable slot 13 (byte offset 52), WASM type 142:
`(i32,f32,i32,f32)->i32`. Existing checked gravity access provides a host-side
validation pattern. A native-only trampoline experiment can preserve stock WASM
equality while observing solver calls.

Illustrative pseudocode; `StepBridge`, table lookup, signature conversion and
validation must be implemented against the actual pinned wasm2c runtime:

```c
typedef u32 (*WorldStep)(void *, u32, f32, u32, f32);

static u32 hooked_step(void *cookie, u32 world,
                       f32 dt, u32 substeps, f32 fixed_step) {
  StepBridge *b = cookie;
  WorldStep original = (WorldStep)b->original.func;
  if (!bridge_world_matches(b, world))
    return original(b->original.module_instance, world, dt, substeps, fixed_step);
  before_solver(b, world);
  u32 result = original(b->original.module_instance,
                        world, dt, substeps, fixed_step);
  after_solver(b, world); /* NOT gameplay_tick_complete */
  return result;
}
```

Require stock profile, valid memory/table bounds and exact signature; save both
the original function **and module_instance**. Prevent recursion, handle shared
funcrefs/unrelated calls, restore before teardown and rebind after world rebuild.
No unchecked function-pointer casts or guessed Bullet layouts are acceptable.

Keep event names explicit:

```text
fixed_step_begin
before_solver -> original solver -> after_solver
original replay application -> After Physics scripts
gameplay_tick_complete
render_begin -> render_end
```

The agent checked `f667(1)` after replay processing and the active gameplay
counter increment after that. The loop continues beyond the increment through
`0x13BEEF`; the counter is an ordering landmark, not a proven complete-tick hook.
Inspect remaining per-step work, early exits and lifecycle before choosing
the full-tick hook. Do not make a single ambiguous `on_tick` callback cover all
of these stages. Adding imports shifts absolute WASM indices; adding helpers
changes function counts. New profiles therefore need named audited exports or
explicit profile-specific metadata, not reused stock ordinal arithmetic.

## 5. Room, compatibility and attempt state machines

```text
Offline -> Creating/Joining -> Lobby -> Connecting -> Loading
    -> BaselineReady -> Armed -> Playing -> Results -> Lobby
                          |         |
                          +-> Abort <-+ (host loss / profile mismatch)
```

The host sends a proposed config; every member acknowledges its canonical hash
after loading and binding roles. Only after all channels/baselines are ready
does the host arm a future start. Clock probes estimate offset, uncertainty and
RTT using monotonic clocks. Use attempt/config-bound `prepare`, `armed` and
`start` messages, with an explicit host-monotonic target timestamp sufficiently
far ahead for measured latency. A client missing the deadline does not silently
start late: it reports invalid/aborted and the room restarts the attempt.

Keep lobby/network/render servicing alive while holding the race at its verified
pre-start gameplay boundary; do not pause the entire host loop or use time-scale
zero as a substitute. Recover this start gate against original map scripts so
hazards/timers cannot advance for one participant while another is loading.

Release is approximately synchronized within measured clock uncertainty, not
atomic: a last-moment partition can deliver start to only some peers. Mark the
attempt invalid when a partial release is detected; readiness ACKs do not solve
distributed atomic start. Define tolerated uncertainty before arming.

Proposed config, serialized canonically and hashed:

```json
{
  "protocol": 1,
  "adapterAbi": 1,
  "engineProfile": "<verified-profile-id>",
  "logicSha256": "<actual-executable-hash>",
  "assetSha256": "<actual-packed-assets-hash>",
  "mapContentSha256": "<map-and-dependency-hash>",
  "variantSha256": null,
  "mode": "friend-race",
  "roomEpoch": 1,
  "attemptId": 1,
  "rules": {"collision": "ghost", "timeScale": 1},
  "slots": [0, 1, 2, 3]
}
```

Do not use display names or numeric menu indices as cross-build map identity.
Rule/mod/catalog changes invalidate readiness and increment the attempt epoch.
Use a separate connection ID for ICE restarts. Local adapter scene generations
reject stale memory references after reload; they are not shared wire IDs. On
the wire use the host's world epoch for co-op and owner-scoped assembly generations
for race worlds. Never compare two peers' unrelated local reload counters.

### Finder protocol

Proposed authenticated envelope:

```json
{
  "v": 1,
  "type": "ice",
  "roomEpoch": 4,
  "connectionId": "<random-negotiation-id>",
  "to": "<server-assigned-peer-id>",
  "payload": {"candidate": "candidate:...", "mid": "0"}
}
```

Allowed signaling kinds: create/join/welcome/member-left/offer/answer/ice/error.
Game readiness/start/results travel over membership-bound control channels after
connection; do not mix state snapshots into the finder. Bind each connection's
SDP/fingerprints and candidate stream to server-authenticated peer IDs, room epoch
and connection ID. Verify the expected offerer/answerer and reject replaced
connections or unexpected/duplicate channel labels. An encrypted edge alone is
not proof that the sender is the intended room member. Assign a deterministic
offerer (host) per star edge to avoid glare. Derive sender ID from authenticated
connection state; validate destination, schema, size, epoch, membership, expiry
and rate before forwarding.

## 6. Actual transport API example

This example uses upstream-documented C API names/fields verified in `rtc.h`,
but **has not been compiled** here. It is not a drop-in complete connection:
callbacks, error paths, credentials, signaling and lifecycle remain to implement.

```c
#include <rtc/rtc.h>

static int make_host_edge(void) {
  const char *ice[] = {"stun:stun.cloudflare.com:3478"};
  rtcConfiguration cfg = {0};
  cfg.iceServers = ice;
  cfg.iceServersCount = 1;
  cfg.disableAutoNegotiation = true;
  cfg.maxMessageSize = 65536; /* initial proposed hard bound */
  int pc = rtcCreatePeerConnection(&cfg);
  if (pc < 0) return pc;

  /* REQUIRED HERE: register local-description, local-candidate, connection,
     incoming-channel callbacks and a live, ref-counted user context.
     Check every registration and cleanup on error. */

  int control = rtcCreateDataChannel(pc, "control");
  rtcDataChannelInit init = {0};
  init.reliability.unordered = true;
  init.reliability.unreliable = true;
  init.reliability.maxRetransmits = 0;
  int state = rtcCreateDataChannelEx(pc, "state", &init);
  if (control < 0 || state < 0) {
    if (state >= 0) rtcDeleteDataChannel(state);
    if (control >= 0) rtcDeleteDataChannel(control);
    rtcDeletePeerConnection(pc);
    return RTC_ERR_FAILURE;
  }
  /* REQUIRED HERE: install open/message/close callbacks for both channels. */
  if (rtcSetLocalDescription(pc, "offer") < 0) {
    rtcDeleteDataChannel(state);
    rtcDeleteDataChannel(control);
    rtcDeletePeerConnection(pc);
    return RTC_ERR_FAILURE;
  }
  return pc; /* real code stores all IDs and context in a connection object */
}
```

Answerer uses `rtcSetDataChannelCallback`, installs the remote offer with
`rtcSetRemoteDescription`, then explicitly answers using
`rtcSetLocalDescription(pc, "answer")`. Queue candidates until remote description
installation, then use `rtcAddRemoteCandidate`. Copy callback strings before
returning; library-owned pointers are not retained by the game.

Use reliable ordered `control` for baseline/spawn/reset/outcome and unordered
zero-retransmit `state` for replaceable snapshots and redundant input bundles.
Channels share congestion; large reliable transfers can still harm state
latency. Check `rtcGetBufferedAmount`; discard superseded **unsent** state rather
than enqueueing unbounded old snapshots. Do not assume the library removes data
already submitted to its queue.

### Optional TURN, never a shipped master secret

Verified provider request, executed by a future authorized server only:

```http
POST https://rtc.live.cloudflare.com/v1/turn/keys/{TURN_KEY_ID}/credentials/generate-ice-servers
Authorization: Bearer {SERVER_ONLY_API_TOKEN}
Content-Type: application/json

{"ttl":7200}
```

Two hours is illustrative, not a decided session policy. Return temporary
credentials to authorized members; redact logs. The C API accepts URI strings,
not the browser-style `iceServers` JSON object. Build credential-bearing TURN
URIs with percent-encoded user/password according to the pinned library docs.
Test expiry during a live allocation; do not assume browser credential-refresh
APIs exist in this C API. Use bounded session duration or verified connection
recreation. Evaluate libnice separately for TURN TCP/TLS.

### Finder hibernation example

Verified Cloudflare API shape; application helpers below are **unimplemented
pseudocode**, intentionally named as such:

```js
import { DurableObject } from "cloudflare:workers";

export class FriendRoom extends DurableObject {
  async fetch(request) {
    // Proposed helpers: authenticate BEFORE accepting a socket.
    const member = await authenticateAndReserveSlot(this.ctx, request);
    if (!member) return new Response("Unauthorized or full", { status: 403 });
    const [client, server] = Object.values(new WebSocketPair());
    this.ctx.acceptWebSocket(server);
    server.serializeAttachment({
      peerId: member.id, roomEpoch: member.epoch,
      membershipId: member.membershipId, expiresAt: member.expiresAt,
    });
    return new Response(null, { status: 101, webSocket: client });
  }
  async webSocketMessage(ws, message) {
    const sender = ws.deserializeAttachment();
    // Proposed helper must enforce UTF-8 byte size, schema, auth, epochs,
    // quotas, authoritative membership, expiry/revocation and connection
    // replacement; attachments preserve data, not current authorization.
    // Never forward a client-supplied sender ID.
    await validateAndForwardSignal(this.ctx, sender, message);
  }
}
```

Make reservation/capacity checks safe under async interleaving; never reserve
the fifth member because two simultaneous requests saw three. Recover room
metadata and socket identity after hibernation. Define SQLite class migrations
(`new_sqlite_classes`) and local Miniflare tests before any remote deploy.
Release reservations on failed upgrades and expire stale reservations without
leaking member capacity. Revalidate current authorization on relevant messages,
not just the original upgrade.

## 7. Wire format, parsing and resource budgets

Define an explicit endian-stable format. Do not `memcpy` a C struct onto the
wire: padding, endianness and alignment are not portable. Example packet header:

```text
magic[4]="DMP1" | version:u8 | kind:u8 | flags:u16
roomEpoch:u32 | attemptId:u32 | worldEpoch:u32
sequence:u32 | simulationTick:u32 | payloadBytes:u16
payload[payloadBytes]
```

Peer identity is established by its authenticated transport edge, not by a
spoofable payload slot. A host-forwarded race snapshot contains the original
owner slot and the host enforces that association before forwarding.
`worldEpoch` is host-authoritative in co-op. Race payloads carry an owner-scoped
assembly generation; local adapter generations never cross this boundary. This
header is 30 bytes. With a 65,536-byte total-message limit, maximum payload is
65,506 bytes, not 65,536. Enforce total and per-kind payload bounds separately;
the transport's `maxMessageSize` setting is not the application parser.

Safe parser fragment, proposed standalone C example:

```c
static bool read_u32_be(const uint8_t **p, size_t *remaining, uint32_t *out) {
  if (*remaining < 4) return false;
  const uint8_t *b = *p;
  *out = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
         ((uint32_t)b[2] << 8) | (uint32_t)b[3];
  *p += 4;
  *remaining -= 4;
  return true;
}

/* Half-range wrap rule; compare only within a bounded window. */
static bool sequence_newer(uint32_t a, uint32_t b) {
  uint32_t distance = a - b;
  return distance != 0 && distance < UINT32_C(0x80000000);
}
```

Proposed initial bounds (tune with measured maps, never silently truncate):

| Resource | Initial proposal |
| --- | --- |
| Players / star edges | 4 / 3 |
| Signaling envelope | 64 KiB maximum including SDP; ICE candidate <= 4 KiB |
| Incoming application message | 65,536 bytes total maximum; header 30 bytes, payload <= 65,506; per-kind smaller limits |
| Ordinary state chunk | Aim <= 1,200 application bytes initially; this is not a guarantee of unfragmented DTLS/SCTP transport |
| Snapshot send rate | 20 Hz first; measure 15–30 Hz |
| Interpolation history | About 1 second; begin at 100 ms presentation delay |
| Extrapolation | <= 100 ms; then freeze/fade with connection indicator |
| Per-peer unsent state | At most latest complete snapshot, with bounded chunk staging |
| Baseline assembly | Bounded total/count/timeout; proposed 4 MiB ceiling pending actual map profiling |
| Peer state silence | Mark degraded after 0.5 s; disconnect policy after 10 s, tune in tests |

For large co-op worlds: chunk with snapshot ID/count/index, byte/count bounds,
expiry and integrity hash; never apply an incomplete baseline. Delta snapshots
reference an acknowledged baseline and have periodic full refresh. Reliable
spawn/despawn and unreliable state can arrive in different orders: gate state
on entity generation, scene epoch and installed baseline. Use bounded unknown-ID
staging or request resync, never arbitrary allocation driven by remote IDs.

Each live snapshot declares baseline ID and entity/topology revision. Choose
atomic bounded assembly for multipart live snapshots initially: do not apply a
partial snapshot or delta referencing a missing baseline. Keep only a bounded
number of in-flight assemblies, expire missing chunks, and request a fresh full
baseline when reliable spawn/topology events or delta dependencies are absent.
An independently applicable per-entity update format is a possible later version,
not something mixed into the same atomic schema without explicit flags/versioning.

Quantize only after rendering/physics bounds are measured. Reject nonfinite
coordinates/rotations and impossible part counts before the engine adapter.
For quaternion presentation normalize safely and handle antipodal interpolation;
the original replay's planar rotation format is not a universal 3-D contract.

## 8. Map inventory, extraction and role discovery

### 8.1 Read-only package inventory example

This uses **existing** helper APIs and writes only stdout; it does not unpack
or modify assets. The content agent used this kind of inspection for research.

```js
import { loadInputs, decodeAsset, sha256 } from "./scripts/lib/modding.mjs";
const { metadata, data } = await loadInputs();
for (const file of metadata.files) {
  if (!file.filename.startsWith("/assets/games/")) continue;
  const packed = data.subarray(file.start, file.end);
  const decoded = decodeAsset(packed);
  console.log(JSON.stringify({
    path: file.filename,
    packedBytes: packed.length,
    decodedBytes: decoded.data.length,
    encoding: decoded.encoding,
    decodedSha256: sha256(decoded.data),
  }));
}
```

Next recover actual game/block record framing and reference resolution through
the existing loaders (`f1567`, `f281`, `f387` and decoder callees). Preserve raw
bytes and report unsupported records; do not write a lossy round-trip packer.
Verify decoder output against real initialized level selection/runtime traces.

Prioritize `Dual Drive`, `Team Work`, `Double Trouble`, `Chick Swap`; these are
confirmed embedded names, **not verified co-op eligibility**. Use the decoded
offsets/script labels in research section 10 as leads, never as lasting map IDs.

### 8.2 Per-map worksheet and catalog

```json
{
  "catalogVersion": 1,
  "sourceAsset": "/assets/games/drive_mad_2",
  "sourceContentSha256": "<measured>",
  "recordIdentity": "<decoded-identity-not-guessed-menu-index>",
  "title": "Dual Drive",
  "runtimeMapping": null,
  "classification": "unverified-candidate",
  "raceSupport": "unverified",
  "coopSupport": "unverified",
  "vehicleAssemblies": [],
  "evidence": ["embedded-name-marker:4266180"],
  "testedPlayers": [],
  "variant": null
}
```

For each map capture:

- Complete vehicle roots, part/template identities and constraints—not body count.
- Controller scripts/input sensors, local/global variables, motors and ownership.
- Vehicle switching, trailers, passengers, transformations, detached parts and
  dynamically created assemblies.
- Camera target/follow logic; finish, crash/global-loss and checkpoint conditions.
- RNG seed consumers, scripted moving props, object lifetime and dependency closure.
- Original single-player behavior before any adaptation.

Classification is a reviewed result, not name matching: ordinary race, race with
multi-assembly ghosting, multi-role co-op candidate, template/menu/test, or a
documented unsupported/blocked case. A blocked playable map remains an unmet
all-maps requirement, not something silently omitted from the denominator.

### 8.3 Extract cooperative variants additively

Only after the format and controller contracts are recovered:

1. Preserve original asset and map record hashes/attribution.
2. Resolve dependency closure: referenced blocks, vehicle templates, scripts,
   materials, textures, sounds and nested records.
3. Create a **new variant** with explicit patch records, not destructive edits to
   the original map. Prefer the existing pack/asset tools where they can preserve
   the recovered format exactly.
4. Bind verified controller contexts to session roles. Keep original motors,
   forces, hazards and win/failure logic unless an intentional variant patch is
   required; document every changed rule.
5. Decide 2/3/4-player role allocation per map. Extra cars require validated spawn
   positions, space, collision, camera and solvability, not cloning at origin.
6. Test original-versus-variant provenance and deterministic content hash across
   machines. Catalog the actual tested player counts.

Example **future** reviewed variant manifest (selectors are placeholders):

```json
{
  "variantId": "dual-drive-coop-v1",
  "sourceMapSha256": "<measured>",
  "dependencyClosureSha256": "<measured>",
  "minPlayers": 2,
  "maxActivePlayers": 2,
  "maxRoomPlayers": 4,
  "roles": [
    {"slot": 0, "controllerRecord": "<verified-role-A>", "camera": "owned"},
    {"slot": 1, "controllerRecord": "<verified-role-B>", "camera": "owned"}
  ],
  "remainingSlots": "spectator",
  "restartPolicy": "whole-level-vote",
  "winPolicy": "<verified-original-or-explicit-reviewed-change>"
}
```

This does **not** assert Dual Drive has two proven roles. It demonstrates how a
catalog should express evidence instead of promising four drivers everywhere.
If four active drivers on every co-op map are mandatory, this two-role variant
does not satisfy that requirement and cannot be counted as complete.

## 9. Friend racing vertical slice and all-map expansion

### 9.1 Recover and adapt, do not feed partial replays

Trace `f435`, `f913`, `f320`, `f1402` through generated pseudocode/WAT before
binding them. Capture the local chassis/wheel assembly from existing Versus,
spawn three trusted synthetic friend ghosts, then separate presentation from
the complete-recording assumption. The original replay clamps to its last frame;
never call its decoder with a growing, unvalidated live buffer.

Verify collision isolation first: copied objects create bodies. Demonstrate
ghosts cannot push the local car, trigger sensors, alter prop collisions, affect
scripts or move the camera. Remove the original per-opponent 3-world-Z lane
offset only after that proof. A visual alpha of zero does not remove physics.

Next use the same assembly/template contracts on an ordinary map. This requires
recovering a regular-level owner/assembly selector; the Versus descriptor is
not a general player-car pointer. Stream all owned assemblies if the original
level has several cars or changes the active car. Ordinary-map unknowns are a
phase gate, not grounds for chassis-only placeholders.

### 9.2 Interpolation example

Proposed presentation algorithm (math helpers are pseudocode):

```c
double render_tick = estimated_owner_tick - interpolation_delay_ticks;
if (history_find_bracket(history, render_tick, &older, &newer)) {
  double span = newer.tick - older.tick;
  float t = span > 0 ? (float)((render_tick - older.tick) / span) : 0;
  /* Match stable part IDs; never blend unrelated or newly spawned parts. */
  frame.position = lerp(older.position, newer.position, clamp01(t));
  frame.rotation = shortest_arc_slerp(older.rotation, newer.rotation, clamp01(t));
} else {
  /* Bounded extrapolation only; then freeze and show degraded status. */
  frame = bounded_fallback(history, render_tick);
}
```

Use sample simulation ticks, not packet arrival spacing. Race state forwarded
through the host retains source sample time and owner identity; clock mapping
must accommodate each racer's timeline. Reset buffers after map/attempt/assembly
changes so old parts cannot interpolate into a different car.

### 9.3 Proximity fade example

Math is executable-style C; **render-object selection and opacity application
remain unimplemented engine contracts**. Distances below are normalized by a
verified vehicle size, not guessed engine world units:

```c
static float clamp01(float x) { return fminf(1.0f, fmaxf(0.0f, x)); }

static float friend_opacity(float distance_in_car_lengths) {
  const float near_distance = 0.5f;
  const float far_distance = 3.0f;
  const float near_alpha = 0.15f;
  const float far_alpha = 0.70f;
  float t = clamp01((distance_in_car_lengths - near_distance) /
                    (far_distance - near_distance));
  float smooth = t * t * (3.0f - 2.0f * t);
  return near_alpha + (far_alpha - near_alpha) * smooth;
}

static float smooth_alpha(float old_alpha, float target, float dt) {
  return old_alpha + (target - old_alpha) * (1.0f - expf(-12.0f * dt));
}
```

Apply a shared per-player multiplier to chassis, wheels, attachments and relevant
effects; restore state after each draw. Preserve the local car at full opacity.
Keep a name/color marker visible, including when nearly overlapping. For
multi-assembly levels use nearest relevant local/remote bounding volumes, not
only one root point. Add screen-space overlap refinement if depth alone is
misleading. Test transparent sorting, shadows, depth writes and self-overlap on
all GL backends; a global `glColor` change is not a per-player material system.

The inspected lit shader at initialized address 116620 declares `uniform vec3 tint`
and writes `FRAGCOLOR.a = 1.0`. Confirm which vehicle draws/program variants use
it; those draws cannot fade through ordinary source-alpha blending alone.
Shader support is an explicit engine gate, not a finishing UI detail.
After positive draw-to-ghost ownership mapping,
an illustrative audited shader change is:

```glsl
/* Proposed addition to the exact matched stock lit shader variant. */
uniform float mpOpacity;
/* Preserve original lighting/RGB expression. */
FRAGCOLOR.a = mpOpacity;
```

Initialize opacity to 1 for every ordinary draw/program and set a per-ghost value
only around verified ghost submissions. Cache/check uniform locations per linked
program, restore depth-write/blend state, and audit shadow/depth passes. Reuse
the original mesh helper `f526` where its callers' object/pass contracts permit;
do not guess ghost identity from VBO IDs. Unknown shader source/profile must fail
closed, not undergo a broad text replacement across UI and unrelated programs.

Co-op fade can be milder or adjustable because other cars are physical puzzle
objects. It remains presentation-only and must not hide ownership/interaction.

### 9.4 Finish and fairness

Recover the real original script-to-win/loss event. On the agreed start release,
record monotonic timestamp and physics tick; at the first valid win latch record
both elapsed real time and elapsed ticks. Proposed default is **monotonic elapsed
completion time**, with simulation ticks as a diagnostic; a simulation-time
leaderboard would be a different explicitly named rule. A capped/smoothed
accumulator can slow simulation on a struggling PC, giving extra reaction time;
clock-control disabling alone does not prevent that. Detect sustained simulation
slowdown, expose it, and define the threshold for invalidating/degrading an attempt
before calling scoring complete. Neither clock proves anti-cheat.
Send a reliable idempotent result containing attempt/config hashes, owner slot,
start/finish times/ticks and outcome. Rank the agreed duration, **not message
arrival order**; do not subtract clock-offset estimates unnecessarily when both
duration endpoints use the same local monotonic clock.

Host validates basic consistency but cannot certify a guest's private independent
world. Label results friend-room/unverified; do not advertise anti-cheat. Pin the
seed where required and recovered, or document intentional random variation.
Disable clock scaling, world forces, single-step/pause and gameplay-mod changes
for scored attempts. Reject duplicate/conflicting results and stale attempts.
Test ties, DNF, failed racers, late packets and disconnect after crossing finish.

### 9.5 All-map coverage matrix

For **every** authoritative playable record, record pass/fail for:

```text
load + common start + correct assembly parts + vehicle switching
ghost collision isolation + nearby fade + camera + win + loss
restart + teardown + 2 players + 3 players + 4 players
```

Inventory themed shipped content separately from menu-selectable runtime maps.
Test heavy articulated vehicles and multi-car maps early, not at the end. Do not
claim “all maps” from testing one Versus course or the first 100 menu entries.

## 10. Host-authoritative co-op implementation spike

### 10.1 First proof: two roles in one existing scene

No networking yet. Trace controller contexts and route two independent input
samples through original scripts. Verify one player accelerating cannot control
the other's vehicle accidentally. Prove scripted shared interactions and all
failure/win conditions, then expand to three/four where map design allows.

Global keyboard substitution is not this proof: both A/D and arrows currently
feed the same input stream. A role binding must select a verified script/controller
context, survive controlled lifecycle changes and reject unowned inputs.

### 10.2 Second proof: passive remote view

Use two local processes: host simulates the level; client receives a reliable
baseline plus live snapshots and **does not independently evaluate authoritative
physics/scripts**. Recover enough state/presentation to show:

- Dynamic bodies, chassis, wheels, articulated attachments, crates and hazards.
- Script-controlled nonphysical transforms, visibility, colors, particles and
  animation as required by the map.
- Spawn/despawn, ownership changes, joints/motors and switch/checkpoint state.
- Shared finish/loss/restart state and a camera following the client's role.

A visual replica may not require exposing every hidden script register to the
client, but the host must remain sole source of authoritative outcomes. If
client scripts must keep running for rendering, explicitly split presentation
from game authority or prove why they cannot alter replicated state.

### 10.3 Input authority example

Pseudocode; `owns_role` and `engine_route_original_controller` need recovered
contracts. Never look up a body from a sender-provided memory address.

```c
MpStatus accept_input(MpPeer *peer, const MpInputPacket *packet) {
  if (packet->attempt_id != session.attempt_id) return MP_STALE;
  if (!owns_role(peer->slot, packet->role_id)) return MP_NOT_OWNER;
  if (!within_tick_window(packet->scheduled_host_tick, session.host_tick))
    return MP_BAD_TICK;
  if (!valid_actions(packet->actions)) return MP_BAD_INPUT;
  if (!input_history_insert_bounded(peer, packet)) return MP_RATE_LIMIT;
  return MP_OK;
}

void before_host_physics_tick(uint32_t tick) {
  for_each_active_role(role) {
    MpActions actions = input_history_actions_for_tick(role, tick);
    engine_route_original_controller(role.binding, actions);
  }
}
```

Guests send current held state and a short history of recent samples, e.g. 3–6,
so unreliable loss does not strand controls. Distinguish held actions from
one-shot edges; each one-shot has a unique sequence and is applied at most once.
Release inputs on disconnect/stale input timeout.

Before implementing the pseudocode, define tick mapping: guest samples include
an estimated host target tick using measured offset and a small agreed input
delay. Host clamps scheduling to a bounded future window; late samples do not
rewind the world without a separately proven rollback feature. Deduplicate by
input sequence, use last valid held state for a short missing-input gap, then
neutralize after a documented timeout. Host acknowledges the last processed input
sequence and its actual execution tick. A guest-local tick is never compared
directly with a host tick as if they shared an origin.

### 10.4 State authority and responsiveness

Start with interpolation/no prediction. This adds guest control latency but
provides an honest correctness baseline. Measure playability at 30/80/150 ms RTT
before promising responsive co-op. Prediction/reconciliation is a separate phase
requiring capture/restore of the relevant controller/physics/script state.
Do not implement rollback by restoring only body positions.

Host-authoritative clients need visual transforms and durable gameplay events;
if clients also simulate physics for prediction, they additionally need correct
velocities, activation, constraints and correction rules. Do not conflate a
passive visual baseline with a full transferable world save. Seamless host
migration remains out of scope until the latter is recovered.

### 10.5 Restart, loss and role vacancy

- Whole-room restart vote queues one safe original teardown/reload, increments
  attempt/scene epochs and waits for fresh baselines/readiness.
- When a guest leaves, release controls; choose pause/restart/lobby according to
  the map's required roles. Never let a required role silently become an obstacle.
- Host loss ends the attempt with clear UI; no implicit local continuation ranked
  as a completed co-op run.
- Keep multiplayer saves/progression isolated from normal single-player records
  until the original persistence semantics have been audited.
- Individual respawn is deferred unless full assembly/controller/contact cleanup
  is proven on that variant.

## 11. UI and diagnostics scope

Later UI implementation must load the applicable frontend/design skills then;
this research phase introduces no UI. Extend the existing native ImGui workbench
or a dedicated in-game multiplayer surface rather than reinstating a webview.

Minimum flow: Create room / Join invite -> mode + map -> role/vehicle selection
where verified -> Ready -> countdown -> race/co-op HUD -> results -> rematch.
Show connection state, player/role ownership, direct versus relayed path, room
capacity, missing-content/profile errors, host departure and connection failure.

Diagnostics: RTT/jitter, loss/reorder, selected candidate path, state buffer age,
snapshot bytes, queue depth, interpolation delay, host tick and scene/attempt
epochs. Redact IPs, SDP, invite and TURN secrets by default. Provide a copyable
redacted report; do not silently upload telemetry or crash dumps.

### 11.1 Discord RPC / Rich Presence

**Planned feature only; no integration, application registration or SDK download
has been performed.** Add optional Discord Rich Presence through the locally
running Discord client. It is a social/presentation layer, **not** the matchmaker,
game transport or a requirement to play offline/online.

#### Intended presence

| Game state | Example Discord activity |
| --- | --- |
| Menus / single-player | `In menus` or `Driving — <map>` |
| Private lobby | `Friend race — In lobby`, party `2 / 4` |
| Race | `Racing — <map>`, party `4 / 4`, elapsed attempt time |
| Co-op | `Co-op — <map>`, active players / room capacity |
| Results | `Race finished` / `Co-op completed`; no unverified global rank |

Use uploaded, licensed application artwork and fallback assets when map art is
unavailable. Presence timestamps are display-only wall-clock values derived from
session state; they must not become the authoritative race/network clock.
Reflect room membership accurately, distinguishing active co-op roles from any
spectators rather than labeling spectators as active drivers.

#### SDK/backend selection gate

- Prefer evaluating the **current Discord Social SDK** for native Rich Presence.
  Official docs describe unauthenticated RPC presence using a valid Application
  ID and a running Discord client; basic presence must not force OAuth/account
  linking. Separately verify requirements for the chosen invite flow.
- The old `discord/discord-rpc` library is deprecated and the Game SDK is legacy/
  archived. Do not introduce either as a supposedly current supported default.
- Current Social SDK C++ getting-started instructions require **C++20+**, while
  this game currently builds C++17. Audit the pinned SDK's C/C++ API choices,
  compiler/runtime compatibility, supported OS/architectures, licensing and
  redistributable binaries before selecting it. Do not silently upgrade the
  entire game toolchain or drop macOS ARM64 support.
- Keep an integration adapter with a disabled/no-op backend and an explicit
  build option. If official SDK packaging is unsuitable, evaluate documented
  local **IPC** RPC behind that boundary; do not use deprecated localhost
  WebSocket RPC or scrape Discord/user tokens.
- A public Application ID is configuration, not a secret. Never ship a Discord
  client secret, bot token or user access token. App creation/art upload and any
  account/portal configuration require a separate authorized setup step.

#### Implementation shape and example

Own SDK/IPC lifecycle in `native/integrations/discord_presence.*`; expose a narrow
C-compatible interface to the session controller. Feed it immutable snapshots
of mode, map, phase, party size and joinability. Pump callbacks without blocking
the main thread, coalesce repeated updates, and use documented rate limits plus
bounded reconnect backoff. Never update presence every physics tick.

Illustrative **proposed adapter API**, not an existing SDK function or compiled
example:

```c
MpPresenceView view = {
  .details = "Friend race — Dual Drive",
  .state = "In lobby",
  .party_size = 2,
  .party_capacity = 4,
  .joinable = room.has_free_slot && room.invites_enabled,
};
/* Copies bounded strings; disabled/unavailable backend is a harmless no-op. */
discord_presence_publish(&view);
```

Actual SDK binding must use the pinned SDK's verified API. Official Social SDK
examples use `Client::SetApplicationId`, `Activity`, `ActivityParty`,
`ActivitySecrets`, and `Client::UpdateRichPresence`; invite callbacks and launch
registration must be verified separately rather than invented from those names.

#### Join/invite behavior and privacy

1. Ship status-only presence first; add Discord Join/Game Invites once existing
   room admission and launch handling are stable.
2. Show Join only for an explicitly joinable room with free capacity. Mid-attempt
   joins follow the existing spectator/wait-until-next-attempt policy. Clear
   joinability on lock/full/expiry/disconnect; server admission remains authoritative.
3. Publish an opaque, scoped, expiring invite capability—not ICE addresses, TURN
   credentials, a room-admin token or a permanent reusable master invite. Treat
   the join secret as potentially forwarded and revalidate it server-side.
4. Route a join callback or registered launch/deep-link payload into the **same**
   room-token redemption, compatibility checks and four-player capacity handling
   as manual invites. Discord identity/presence is not proof of room membership.
5. Validate payload length/scheme/token format; never execute payload text as a
   command, arbitrary URL or filesystem path. Queue launch intents until startup
   completes and confirm before abandoning an active attempt. Define single-
   instance forwarding so an invite does not accidentally start competing clients.
6. Provide an explicit Rich Presence toggle with a conservative disabled default,
   plus separate map-detail and invite-sharing controls. Explain that activity is
   shared on Discord; private rooms must not become publicly discoverable by
   accident. Do not log invite secrets or personal Discord identifiers by default.
7. Discord absent/closed/restarted, browser-only Discord, IPC denial or SDK failure
   must leave all game features intact. Clear activity on disable/normal exit;
   tolerate crashes and reconnect without blocking shutdown.

#### Sources and acceptance gate

Official documentation checked **2026-11-18**; recheck before choosing a version:

- https://docs.discord.com/developers/discord-social-sdk/development-guides/setting-rich-presence
- https://docs.discord.com/developers/discord-social-sdk/development-guides/managing-game-invites
- https://docs.discord.com/developers/discord-social-sdk/getting-started/using-c++
- https://docs.discord.com/developers/topics/rpc
- https://docs.discord.com/developers/rich-presence/overview
- https://github.com/discord/discord-rpc (deprecation notice)

Test status transitions, timestamps, artwork fallback, party size 1–4, full/locked
rooms, expired/revoked/malformed invites, joining while already in a session,
Discord start/stop/restart, opt-out, offline play, no authentication, bounded
updates, clean exit and packaged binaries on every supported OS. Validate SDK
availability/architecture and invite behavior in real Discord clients, not only
mock callbacks. Use a private test account/server because presence is visible.

## 12. Verification plan and definition of done

### Unit and parser tests

- Cross-language golden serialization fixtures/endian behavior; all truncation
  boundaries; unknown kinds/versions/flags; overflow-safe lengths/counts.
- NaN/infinity, malformed quaternions, duplicate part IDs and huge allocations.
- Sequence wrap, half-range ambiguity, stale/future ticks, scene generations,
  duplicate finishes, ownership spoofing and out-of-order baseline/state.
- Fade curve endpoints/monotonicity, smoothing bounds and local-car opacity.
- Map decoder bounds, dependency cycles, malformed references and lossless
  original bytes; manifest hashes are reproducible.

### Finder/local service tests

- Concurrent fourth/fifth joins, wrong/expired tokens, enumeration throttling,
  spoofed sender/destination and oversized signaling frames.
- Deterministic offerer, candidates before SDP, stale reconnect epochs and room
  expiry while clients are disconnected.
- Hibernation/reconstruction preserves memberships and host identity correctly.
- Quota exhaustion/service refusal gives a clear retryable error, not a crash.
- Secret redaction and temporary TURN issuance limits; no shipped master key.

### Native integration and graphics

- One local + three synthetic ghosts on original Versus, then ordinary maps.
- Ghosts never alter physics, sensors, camera or local completion.
- All-map matrix from section 9; articulated/swapping/detachable vehicles.
- Multiple close ghosts, fade sorting/shadows, GL state restoration on Linux
  GLES2 and Windows/macOS desktop GL; no game asset/body-pointer lifetime leaks.
- At least 100 room restart/load/leave cycles with stable world/camera counts,
  memory plateau and zero invalid engine references.
- Preserve single-player, original replay GET allowlist/upload blocking, native
  SDK/plugin lifecycle and save isolation regressions.

### Multiclient/network tests

Build a bounded four-process harness with isolated save dirs and deterministic
test inputs. Add a transport shim for controlled loss/jitter/reordering before
real Internet testing. Proposed matrix:

| Dimension | Cases |
| --- | --- |
| Players | 2, 3, 4; reject fifth; host + each guest leaves |
| RTT | 0, 30, 80, 150, 250 ms |
| Loss | 0%, 1%, 5%, 10%, burst loss |
| Jitter/reorder | 0–50 ms jitter, delayed duplicates, reordered state/control |
| Reachability | Same LAN, different home NATs, mobile/CGNAT, IPv6, VPN, UDP blocked |
| Paths | Direct success, direct failure, forced TURN, TURN unavailable/expired |
| Platform mix | Windows x64 ↔ Linux x64 ↔ macOS ARM64 |
| Lifecycle | Alt-tab, resize, dropped Wi-Fi, reconnect, finder outage, late join |
| Timing | Different render rates, catch-up substeps, pause/time-scale attempts |
| Co-op | Two independent controls, shared crate/bridge/switch, failure and team finish |

Add per-platform native tests instead of relying on the existing Linux-only
`built` predicates. Run `npm run native:build`, then GUI-enabled `npm test` where
a graphics session exists, as required by `AGENTS.md`. Record real Internet
manual evidence separately from synthetic local transport results.

### Proposed performance targets, not measurements

- Four-player normal race remains responsive with no network waits on the game
  thread; aim below 2 ms p95 added main-thread work on a declared reference PC.
- State queues remain bounded even when receivers stop reading.
- No old snapshot backlog grows during congestion; diagnostics expose degradation.
- Co-op bandwidth scales with actual map complexity; record worst cases before
  accepting relay budgets or quantization bounds.
- Ordinary frame timing and single-player startup remain unchanged when offline.

### Final acceptance checklist

- [ ] Four actual simultaneous racers complete every agreed existing playable map.
- [ ] Three correctly assembled friends stay visible and fade smoothly nearby.
- [ ] Common starts, genuine win events, elapsed-time ranking and DNF work.
- [ ] Private room finder uses the selected free plan without hidden game relay.
- [ ] Direct-only failure and optional relay/cost/privacy policy are explicit.
- [ ] Cooperative collection contains only reviewed, proven multi-role variants.
- [ ] Each variant states tested player counts; required four-role maps actually
      have four active drivers, not two silently relabeled as four-player support.
- [ ] Host-authoritative shared objects/scripts, ownership and remote cameras work.
- [ ] Build/profile/content mismatches fail safely; unknown logic never receives
      stock private offsets through a forced feature flag.
- [ ] Cross-platform packaging, dependencies, licenses, tests and save isolation pass.
- [ ] Optional Discord Rich Presence reports accurate mode/map/party state, honors
      privacy settings, and never makes Discord installation/login mandatory.
- [ ] If enabled, Discord invites use scoped tokens and normal room admission;
      full/locked/expired rooms and unsupported launch paths fail safely.
- [ ] Deployment and release have separate explicit user authorization.

## 13. First implementation assignment after approval

Do **not** begin by writing a public matchmaker or a four-car replacement demo.
Begin with the reproducible baseline and original-engine discovery gate:

1. Install/build in the documented toolchain without overwriting existing work.
2. Generate missing research artifacts and authoritative map inventory.
3. Trace original vehicle assemblies/controllers, real tick boundaries and finish.
4. Prove synthetic ghost collision isolation and per-player material opacity.
5. In parallel, build only an engine-independent protocol/loopback test library
   and local-only room-service tests against agreed interfaces.
6. Review evidence, then authorize the first live friend-race vertical slice.

Use the agent work packages above during implementation. Stop and revise the
plan at failed engine gates rather than disguising prototypes as verified game
functionality. Public service deployment, billable relay enablement and release
publication remain separate decisions.
