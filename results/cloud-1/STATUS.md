BUSY PRM-028
RESULT:
- Working on PRM-028 (high), claimed 2026-10-16T22:15:36Z: #6636 snapshot-write sync with a draw red, branch `pm/6636-snapshot-writes`.
- Last finished: BF-040 (high), results/cloud-1/BF-040.md.
  - Reuse of address-based dispatch builds, stacked on #6636 (`89ab0ea75`): `perf/agc-reuse-address-dispatch-builds` at 1660019df (9dc75640f, 0beb19604, 1660019df). No PR is open.
  - BF-033 harness (lavapipe), µs per dispatch: 16 textures 519 → 76; 2,000 textures 5,032 → 699; 8 per submission 1,600 → 28.
  - All 6 new cases are red on #6636's head and green on the branch; new test ×20 passed.
  - `^agc`: reuse on 608/608, reuse off 608/608, base 607/607. BF-033's 11 stalling tests pass with reuse on.
- Before that: BF-037.
