BUSY GAC-036 (claimed 2026-10-15T18:02:16Z)
RESULT:
- Working on GAC-036: on Windows, a storage image's pending GPU results are dropped after its memory is imported (write-watch stamp order). Branch fix/agc-write-watch-stamp-order on upstream main.
- Last task: GAC-033, done: results/cloud-1/GAC-033.md.
  - fix/agc-sample-location-grid-exact 57ceb9e37; diag/village-ms-consumers 265937aea.
  - ^agc 605/605 at 2d6079340; merge with main 03c4cb5e78 609/610 (the 1 fails on pure main too: llvmpipe LLVM crash in agc_driver_mesh_index_cache).
  - Coordinator runs: --grid-reference with ANYPS5_GPU=Radeon and =3070, and --bench 1920x2160 2 100 on the 3070.
