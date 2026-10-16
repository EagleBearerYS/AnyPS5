BUSY BF-040
RESULT:
- Working on BF-040 (high), claimed 2026-10-16T20:12:19Z: reuse of address-based dispatch builds, stacked on #6636 (`perf/agc-reuse-address-draw-builds` 89ab0ea75a). Branch `perf/agc-reuse-address-dispatch-builds`.
- Last finished: BF-037, results/cloud-1/BF-037.md.
  - The heap ceiling prototype is on `exp/descriptor-heap-ceiling` 8c8249d1a.
  - With the heap, rebuilds after one image changed drop 15.4 ms → 0.45 ms (N=8) and 59.7 → 43.1 ms (N=4,096; 31.8 ms with the image skip).
  - Unchanged draws are bound by per-element Revalidate (93 % at N=4,096).
  - `^agc` 625/625 with the heap off and on.
- Before that: BF-038.
