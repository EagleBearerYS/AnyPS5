READY
RESULT:
- Last task: GAC-017, done: results/cloud-1/GAC-017.md.
  - f146a9ade2 (fast-main with PM-027 + PM-028) builds with CI's flags: configure, registration ("890 test executable(s), 0 built and not run as recorded, none new"), build and libs exit 0, no error or warning.
  - `^agc`: 3 of 585 fail: GAC-010's two sample experiments and the known agc_driver_preparation_scope_settle flake (passed alone 20/20). 41 skips, main's.
  - The 9 image-table and PM-028 tests pass, including agc_shader_table_snapshot_reuse.
  - The merged [image-table] (10 s) line: no test prints it. It checks out statically (12 specifiers/12 arguments), with GCC -Wformat, and at runtime via a scratch clock shim, where every field printed.
  - 70312aa432 (-locallazy): configure, registration, build and libs exit 0 (not tested, as asked).
- Task before it: PM-031, done: results/cloud-1/PM-031.md.
  - Fork branch test/fsr-trace-main = acef210b9 (pushed): upstream main a077a88df5 (= the current tip) + merge 03a4b4595 of FSR 67baf029ee + merge acef210b9 of the trace b2cecfc2f5. Both merges clean, no fix commit.
  - CI configure, registration ("883 test executable(s), 0 built and not run as recorded, none new"), build and libs exit 0.
  - `^agc|video_out|guest_video_out|guest_system_service`: 600 run, 1 failed (the known agc_driver_preparation_scope_settle flake). 58 skips, the same as main. FSR tests 2/2. Trace check with APS5_TRACE_DISPLAY_QUERIES=1: 7 pass, 3 skip (environment), 0 fail.
  - Windows traps: no far/small/thread_local. One `near` lambda in FSR's tests/PresentationPass.cpp, not a break today. PM-027's PR branch still has cut #3's `far` (tests/ShaderMemory.cpp:1878).
  - Boot: ANYPS5_UPSCALE=fsr (optionally ANYPS5_FSR_SHARPNESS 0.0-2.0, default 0.2) and APS5_TRACE_DISPLAY_QUERIES=1.
