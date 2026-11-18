# Existing Versus / ghost-racing functionality

Research date: 2026-11-17. Applies to stock game version 166 only.
Original recorded races are restored in the native host. Live friend
multiplayer is **not implemented**.

For the proposed 2–4-player live friend races and cooperative map variants, see
[live multiplayer research](LIVE-MULTIPLAYER-RESEARCH.md) and the
[implementation plan with code examples](LIVE-MULTIPLAYER-PLAN.md).
Those documents are research/planning only; this file records the original
Versus functionality and its restoration.

## Finding

The game already contains an online **asynchronous ghost-racing** mode.
It downloads completed runs, creates opponent vehicles, and plays their recorded
transforms alongside the player. The traced path is not a live room/session:
opponents' complete recordings are fetched before the race.

The original service must **not** be described as retired based on failed
requests to its directory URLs. Read-only checks of actual files succeeded:

- `GET https://api.drivemad.com/vs/5/manifest.data`: HTTP 200, 4,866 bytes.
  zlib decompression produced 33,082 bytes of JSON with `upload_chance` and `vs`.
  The manifest listed 35 tracks and 571 recordings.
- `GET https://api.drivemad.com/vs/5/rec_arches_0.data`: 25,033 bytes;
  decompressed size 48,924. Header `dma\0`, format version 9; track `Arches`,
  vehicle `dino`, 1,764 recorded frames, winning frame 1,719. The winning
  frame matches the manifest.

These checks prove manifest/replay downloads work, **not** that uploads or the
entire original mode work in this port. No recordings were uploaded.

## Recovered engine paths

Paths below are under `modding/generated/workspace/pseudocode/` after generating
the decompilation workspace:

| Function | Evidence |
| --- | --- |
| `01664-f1664.dcmp` | Versus entry: sets mode byte at 23223524 to 1 and transitions UI state. |
| `01567-f1567.dcmp` | Loads shipped `assets/games/drive_mad_versus`; initializes race/track and opponent-selection state. |
| `01719-f1719.dcmp` | Requests `/vs/5/manifest.data` through request callback table index 285, then selects opponents by recorded times. |
| `01587-f1587.dcmp` | Callback 285: decompresses and parses the JSON manifest and recording filenames/times. |
| `01717-f1717.dcmp`, `00696-f696.dcmp` | Queues the chosen recording downloads and waits for completion; callback table index 286. |
| `01586-f1586.dcmp` | Callback 286: decompresses/validates the binary recording and decodes vehicle, frame, outcome and transform data into a 1,160-byte replay descriptor. |
| `00435-f435.dcmp:1651–1886` | Finds Versus scene templates, installs downloaded replay descriptors, creates the local vehicle and opponent vehicles, and initializes recording. |
| `00913-f913.dcmp` | Builds chassis/wheels from the existing vehicle/template tables. Local descriptor is at 23226844; opponents use 60-byte descriptors starting at 23226912. |
| `00320-f320.dcmp` | Copies an existing scene object; local and opponent copies take different script setup paths. It also creates a physics body, so collision behavior needs tracing before reusing it for overlapping ghosts. |
| `01402-f1402.dcmp:11379–11758` | Records local chassis/wheel positions and a rotation angle per frame; plays downloaded transforms at the current simulation tick. Opponents are offset by 3 world-Z units per lane. |
| `01018-f1018.dcmp` | Serializes a complete run, including vehicle identity, winning/losing frames and compressed transform deltas. |
| `00288-f288.dcmp:13–149` | On teardown, may compress and queue a completed recording to `/vs/upload/5/rec_…data`, gated by recording state and upload sampling. |

## Native restoration

```bash
./native/build/drive-mad --versus
# Or: F2 -> Fun -> Enable replay downloads and open Versus
```

This enters the original Versus scene. Use its **Race!** button to select a car
and race the downloaded opponents. The original presentation uses separate
lanes; it is not yet overlapping translucent friend ghosts.

`native/versus.c` restores the GET bridge using libcurl's nonblocking multi pump
on the main thread. Only HTTPS requests for the exact original manifest and
`rec_*.data` basenames are allowed. Redirects, arbitrary URLs and every upload
remain blocked. Downloads are opt-in; normal launches remain offline. Both
successes and failures are delivered on a later host tick, because the core
assigns its request ID/state **after** returning from the host bridge. Request
ID zero is valid.

There are at most eight requests, a 2 MiB compressed limit, a 32 MiB inflated
limit, and 5/15-second connection/request timeouts. The complete version-9
replay structure, allocation counts, keyframe floats and delta lengths are
checked before passing it to the stock decoder. This is not a sandbox: use only
trusted replay caches. Manifest JSON is still interpreted by the original engine.
HTTPS uses system certificate trust; verification is never disabled.
The private `f1664` entry binding is available only under the checked stock
executable profile.

For offline diagnostics, `--versus-cache DIRECTORY` reads `manifest.data` and
the referenced `rec_*.data` files from that directory instead. It does not
download, modify, or populate the cache, and missing/malformed files fail through
the original error UI. There is no persistent download cache yet.

Manual validation reached races with three spawned opponents and five tracked
local vehicle parts, and observed opponent chassis transforms changing across
frames with no GL errors. The automated transport test uses only local
synthetic files, checks deferred completion (including ID zero), blocked uploads,
URL restrictions, missing/truncated/oversized data and replay allocation bounds.

## What to reuse for live friend races

Keep the existing vehicle templates, chassis/wheel identification, ghost object
creation and transform application rather than inventing a second car renderer.
The native restoration validates the original download/spawn/playback route;
retain its bounded downloads and disabled uploads while adapting the race flow.

For **simultaneous** races between friends, a new transport/session layer is still
needed: private rooms, common track/vehicle configuration, synchronized start,
bounded live transform updates, buffering/interpolation, disconnect handling and
finish results. The original playback assumes a complete replay and clamps to its
last frame; it must be adapted rather than fed unvalidated partial buffers.

Also verify collision isolation and remove the original separate-lane offset if
both players should overlap as noncolliding ghosts. Access to this Versus-specific
car metadata does not establish a player-car selector for every regular level.
No public server deployment or firewall changes have been made.
