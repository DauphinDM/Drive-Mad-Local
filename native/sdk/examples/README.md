# Example native mods

These are complete, buildable plugins. Copy the ones you want into a mods
directory, build them, then start the game with `--mods`:

```bash
cp -r native/sdk/examples/fun-pack native/build/mods/fun-pack
npm run native:mod -- build native/build/mods/fun-pack
./native/build/drive-mad --mods native/build/mods
```

| Example | What it does |
| --- | --- |
| `native-tools` | General SDK demo: launcher counter, settings, ad callbacks, F6 reward, pause/resume commands. |
| `fun-pack` | F7 gravity roulette, F8 bouncy world, F9 ice rink, F10 freeze; console toy presets. |
| `time-warp` | F9 cycles the game clock; `mod time-warp set 0.35` accepts a custom speed. |
| `couch-chaos` | Local turn-based two-player mode with timer and persistent wins. |

The examples use overlapping F6–F10 keys on purpose to show input consumption;
load only one at a time if the shortcuts would conflict. They are native code,
not sandboxed, and only load when you pass an explicit mods directory.
