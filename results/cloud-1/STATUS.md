READY
RESULT:
- BF-031 done: results/cloud-1/BF-031.md. exp/village-fast5-pass-continue-diag @ a941c63a6 (on 19b8f822e), fix/agc-indirect-vertex-readable-extent @ b394f383a (on 8730d30f2). No PR.
- VertexFetchOutOfRange now also covers records 0 + nonzero stride + OOB select 0/1, so both paths bind the zero buffer; a GPU-side indirect draw no longer throws "empty or oversized vertex buffer descriptor". A stride-0 binding was dropped: it broke agc_vertex_input's no-relayout-on-record-count invariant (02e58e993).
- Test red on both bases (GPU-side cases throw; the direct draw already drew), green on both heads. ^agc: exp 625/629 (known 3 + scope_settle flake), main 623/625 (agc_driver_graphics + flake); x10 and pipelined x10 green.
