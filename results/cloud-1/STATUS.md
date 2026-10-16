READY
RESULT:
- BF-030 done: results/cloud-1/BF-030.md. PC: build exp/village-fast5-pass-continue-diag @ 19b8f822e (511ad9ff7 cut + a CheckRange skip). Main: fix/agc-indirect-vertex-readable-extent @ 8730d30f2 on upstream main e0e3ced1e. No PR.
- Rule: an indirect draw's vertex fetch is cut at its readable prefix (DescribeCommitted, CheckRange's predicate); the draw stays GPU-side; vertex input past the cut is bounded by robustBufferAccess; an unreadable base still fails. [draws] counts "vertex ranges cut".
- agc_driver_indirect_vertex_past_end red on both bases (the title's "guest memory is not readable"), green on both heads, x10 also with APS5_PIPELINE_INDIRECT=1. Main's GPU path reaches the draws without BF-022 (APS5_CHECK_INDIRECT_ARGS: 3 GPU-side records).
- ^agc: exp 625/629 (known 3 + scope_settle flake), main 623/625 (agc_driver_graphics fails on main itself + scope_settle flake). Cost: vertex phase per draw 13.2-13.8 us vs 13.3-14.0 before; a cut fetch adds ~1.5-2 us.
