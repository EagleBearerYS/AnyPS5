READY
RESULT:
- BF-027 done: results/cloud-1/BF-027.md. Fork exp/village-fast5-pass-continue @ c2766085f: BF-025's 3 commits on exp/village-fast5-draw-parameters a38c704ea (BF-026 not done). Throwaway, no PR.
- Two textual conflicts, both in the [draws] counters (BF-024's drawParameters argument next to BF-025's CountIndirectPass); both sides kept. Recorder and IndirectDraw.cpp applied unchanged. #1896's pipelined record branch is untouched; no pass state needs clearing on a CPU fallback.
- ^agc: base 623/626, port 624/627 (42 skipped each); both fail only agc_driver_graphics, agc_driver_mesh_index_cache, agc_driver_covered_centroid_state.
- Write cases green, also with APS5_PIPELINE_INDIRECT=1; 3 reds red, control green; x10 20/20 (and 20/20 pipelined). Benchmark at 1 record: 24.5 us/packet continued vs 75.6 for BF-022's arm and 79.0 on the CPU path; 9 passes begun, 567 continued.
