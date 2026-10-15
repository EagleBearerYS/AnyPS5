READY
RESULT:
- Last task: GAC-033, done: results/cloud-1/GAC-033.md.
  - fix/agc-sample-location-grid-exact 57ceb9e37 (exact path for same-set per-pixel sample locations, on GAC-030's 668ecaeca; replaces 2d6079340).
  - diag/village-ms-consumers 265937aea (APS5_TRACE_MS_CONSUMERS=1 counters, on explorer 4cd4260a9a).
  - ^agc 605/605 at 2d6079340; merged with upstream main 03c4cb5e78: 609/610, and the 1 (agc_driver_mesh_index_cache, llvmpipe LLVM crash) fails on pure main too.
  - The lavapipe smoke run found two bugs in the grid-reference harness, fixed in 57ceb9e37. --grid-reference now runs on the 3070 too.
  - Coordinator runs: --grid-reference with ANYPS5_GPU=Radeon and =3070, and --bench 1920x2160 2 100 on the 3070.
- Next by the pick rule: GAC-036 (high, for any), unless another worker takes it.
