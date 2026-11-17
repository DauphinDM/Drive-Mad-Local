# Couch Chaos

Local, turn-based two-player play for the native build. This is a real hot-seat
mode, not a revival of Drive Mad's original online Versus service and not
split-screen rendering.

```bash
cp -r native/sdk/examples/couch-chaos native/build/mods/couch-chaos
npm run native:mod -- build native/build/mods/couch-chaos
./native/build/drive-mad --mods native/build/mods
```

Controls while the mod is loaded:

- **F7** — start/restart Player 1's run and reset the timer.
- **F8** — hand over: switch player, restart the same level, reset the timer.
- **F9** — claim the win for the current player. The score is persisted in the
  mod's own native storage.

Console commands: `mod couch-chaos info`, `restart`, `switch`, `win`, `reset`.
The window title shows the active player, elapsed time and the running score.
The mod deliberately does not guess a finish line, player-car pointer or
physics offset; the players press F9 at the real finish. It requires the stock
v166 gameplay profile for level restart and fails closed otherwise.
