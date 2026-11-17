# Feature development

- Before implementing a new game feature, investigate whether the original
  engine, scripts, assets or host bridges already provide it. Prefer restoring
  and extending existing functionality over a parallel replacement.
- Record recovered contracts and supporting function references in
  `native/sdk/*-RESEARCH.md`. Keep stock-executable checks around private engine
  bindings; do not guess memory offsets or vehicle identities.
- Distinguish verified features from prototypes: original Versus downloads are
  recorded races, not simultaneous friend multiplayer.
- Preserve existing uncommitted work. Do not publish releases or deploy servers
  unless requested.
- Verify native changes with `npm run native:build`, then
  `DRIVE_MAD_TEST_GUI=1 npm test` when a graphics session is available.
