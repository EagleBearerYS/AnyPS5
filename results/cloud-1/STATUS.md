BUSY BF-009 (claimed 2026-10-15T07:40:02Z)
RESULT:
- Working on: BF-009 (#6243 follow-ups on `perf/shader-direct-cpu-evaluable-images-followup`, on 43855cb5e8).
- Last task: PM-035, done: results/cloud-1/PM-035.md.
  - #5815: gaps found. The GPU BDA table kept the old "greatest begin" lookup over the overlapping lists. Fixed with tests on fork `followup/5815-bda-table` 6d17a1dea (4 commits on c54854b2bd). Head ctest: 1117 run, 5 failed (the socket baseline).
  - #6146 + v3: gaps found. The high one is fixed on fork `followup/6146-address-based-views` d3d4d32c1 (3 commits on bac2e3285f): address-based builds did not store the results pending under their read-only V#s.
    - Red at bac2e3285f: 0 of 15360 texels in both rounds. Green: 15360 of 15360.
    - Head ctest: 1201 run, 5 failed (baseline). It merges cleanly with main db81992c5 and onto v4 6596a1d3a2.
    - Still open, listed with severity: the #6188/#5925 merge-order hazard (high), DB_RENDER_OVERRIDE copies, stencil, D16 and slices, decode drift, and others.
  - #6243: gaps found. They go to BF-009 (in progress).
