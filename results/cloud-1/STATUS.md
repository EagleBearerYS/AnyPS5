BUSY GAC-020 (claimed 2026-10-15T08:57:11Z)
RESULT:
- Working on: GAC-020 (Linux check of #6146's next head fix/agc-db-depth-expand-v4 6596a1d3a2).
- Last task: BF-009, done: results/cloud-1/BF-009.md.
  - Fork `perf/shader-direct-cpu-evaluable-images-followup` 23d6cf684, 7 commits on #6243's head 43855cb5e8.
  - Item 1 (sampler selection): no code. The premise doesn't hold: an S# the SRT walker evaluates is already bound directly (DirectSampler). Pin test 3b914c84b.
  - Item 2: a T# whose record the CPU evaluates is now bound directly when a table can't express its operation (stores, atomics, PCK, BY2/BY4); per-lane keys keep the table fault. FormatVersion 37.
    - Red: "image_store ... was planned through an image table" (and atomic, PCK, BY2, mixed); the GPU store "faulted ... unsupported operation".
    - Green: planned direct; 64/64 texels stored.
  - PM-035 #6243 gaps also fixed, each red/green: NonUniform on a selected record's index (gap 2), and key domains kept on deselection (gap 7). Gaps left, with reasons: 1, 3, 5, 6, 8-14.
  - CI: each commit builds and passes 8/8 related tests; head ctest 1229 run, 5 failed (socket baseline); check_skips: guest_sce_net only; conventions pass; 0 added comments.
  - Clean merge with upstream main 721db55dc; no new conflicts with #6232 or #6032.
- Task before it: PM-035, done: results/cloud-1/PM-035.md. Follow-ups `followup/5815-bda-table` 6d17a1dea and `followup/6146-address-based-views` d3d4d32c1.
