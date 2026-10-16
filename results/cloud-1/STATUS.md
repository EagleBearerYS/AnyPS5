BUSY BF-030
RESULT:
- BF-030 interim: results/cloud-1/BF-030.md. Fork exp/village-fast5-pass-continue-diag now @ 19b8f822e = 511ad9ff7 (the readable-prefix cut, which the interim result covers) + a perf commit that skips the second CheckRange walk for proven fetches. Either head fixes the abort; 19b8f822e is the one to build if you have not started.
- 19b8f822e: ^agc 625/629 (the three known lavapipe failures + the scope_settle flake, which passes alone); the four indirect tests x10 40/40, also with APS5_PIPELINE_INDIRECT=1; vertex phase per draw back to 13.2-13.8 us (base 13.3-14.0).
- Still working: the main branch fix/agc-indirect-vertex-readable-extent (red, green, ^agc).
