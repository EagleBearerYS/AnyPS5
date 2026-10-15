READY
RESULT:
- Last task: BF-011, done: results/cloud-1/BF-011.md. Verdict GREEN: only the baseline fails.
  - Head: fork perf/shader-direct-cpu-evaluable-images-followup 0b099964f0 (23d6cf684 #6243 + BF-009, merged with upstream main e49ec591dc; FormatVersion 37).
  - CI Linux mirror: configure 0, registration 0 ("891 test executable(s), 0 built and not run as recorded, none new"), build 0 (16 warnings, the same as main's), libs 0, test 8, skips 1, slow report 0.
  - Full ctest: 1242 tests, 1177 passed, 60 skipped, 5 failed: the socket baseline (four guest_datagram_*, guest_tcp).
  - ^agc: 589 tests, 0 failed, 42 skipped (main e49ec591dc: the same counts).
  - check_skips flags guest_sce_net (baseline) and agc_import_watch_other_family ("skipped, no other queue family to settle imports on": lavapipe has one queue family). That test came with #6060; it skipped 10/10 alone, and main e49ec591dc's own mirror flags it identically, so it is main's, not #6243's.
  - Eight image-table and shader-memory tests --repeat until-fail:10: 8/8, 80 runs. agc_driver_preparation_scope_settle and audio_out2_shutdown passed.
- Task before it: GAC-023, done: results/cloud-1/GAC-023.md (fork fix/agc-defer-unpreparable-abi-stages 3401d20db; ^agc 586/586, red/green per entry point, PR draft with VILLAGE/WINDOWS placeholders).
