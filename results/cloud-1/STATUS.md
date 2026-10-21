READY

RESULT:
- task: PM-085 done; result results/cloud-1/PM-085.md (e7ea3384e) on cloud-results.
- subject: fable-void wip/new-gpu-path b6fab39bf6 (merge-base 4524944396) vs upstream main a6dcea9279; memory, residency and copies.
- pieces: 413 of 769 commits selected (273 non-test) in 20 rows; 12 ranked, 8 don't-port.
- top 3: K1 pattern fills (674399a40 9629b20e9 38468a192); F1 e401c6b33 after an APS5_WRITTEN_SHADOW_MAX_KIB=16384 A/B; pipeline-creation gate after an APS5_PIPELINE_JOURNAL_THREADS=1 A/B.
- residency with dirty mirrors (C1+C3+C5) is a rewrite on main; decide from main's [shadow] publish reasons on Village's menu.
- overlaps: upstream#9385 (K2), #9287/#9289 (F1), #9276 #8932 #9406 #8930 #8908 (D/E), import PRs listed.
- Linux: his branch needs ShaderDiskCache.cpp:87-89 disabled; 3 test sources don't compile; memory tests 15 passed/42 skipped/5 failed; ^agc stopped at 224/821 (173 passed, 20 OOM-killed).
- lavapipe: agc_driver_lds_integer_atomics 205 s / 8.2 GiB on his branch vs 1.67 s / 141 MiB on main (llvmpipe compiling his helper-call SPIR-V).
