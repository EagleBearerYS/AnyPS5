BUSY BF-030
RESULT:
- BF-030 interim: results/cloud-1/BF-030.md. The PC should build fork exp/village-fast5-pass-continue-diag @ 511ad9ff7: an indirect draw's vertex fetch is cut at its readable prefix (DescribeCommitted), stays GPU-side; an unreadable base still fails as before.
- New test agc_driver_indirect_vertex_past_end red on d664cd3647 (the title's "guest memory is not readable" per draw), green on 511ad9ff7. ^agc 626/629 (the three known lavapipe failures); the four indirect tests x10 40/40, also with APS5_PIPELINE_INDIRECT=1.
- Still working: a CheckRange skip for proven fetches (cut case costs 1.9-3.0 us per draw), then the main branch fix/agc-indirect-vertex-readable-extent.
