BUSY GAC-017 (claimed 2026-10-15T04:16:39Z)
RESULT:
- Last task: PM-031, done: results/cloud-1/PM-031.md.
  - Fork branch test/fsr-trace-main = acef210b9 (pushed): upstream main a077a88df5 (= the current tip) + merge 03a4b4595 of FSR 67baf029ee + merge acef210b9 of the trace b2cecfc2f5. Both merges clean, no fix commit.
  - CI configure, registration ("883 test executable(s), 0 built and not run as recorded, none new"), build and libs exit 0.
  - `^agc|video_out|guest_video_out|guest_system_service`: 600 run, 1 failed (the known agc_driver_preparation_scope_settle flake). 58 skips, the same as main. FSR tests 2/2. Trace check with APS5_TRACE_DISPLAY_QUERIES=1: 7 pass, 3 skip (environment), 0 fail.
  - Windows traps: no far/small/thread_local. One `near` lambda in FSR's tests/PresentationPass.cpp, not a break today. PM-027's PR branch still has cut #3's `far` (tests/ShaderMemory.cpp:1878).
  - Boot: ANYPS5_UPSCALE=fsr (optionally ANYPS5_FSR_SHARPNESS 0.0-2.0, default 0.2) and APS5_TRACE_DISPLAY_QUERIES=1.
- Task before it: GAC-016, done: results/cloud-1/GAC-016.md.
  - test/village-fast-main 00582f7a45 (fast-main with PM-027 in place of the switch helper) builds with CI's flags: configure, registration, build and libs exit 0, no compile error.
  - `^agc`: 2 of 584 fail, both GAC-010's sample experiments (agc_driver_graphics, agc_driver_covered_centroid_state), which pass with the three APS5_RE8_STRICT_* switches. Nothing else fails; the 41 skips are main's.
  - The 8 image-table and PM-027 tests pass.
  - test/village-fast-main-locallazy bf89905763: configure, registration, build and libs exit 0 (not tested, as asked).
  - PM-028 onto 00582f7a45 (scratch merge-tree, not resolved): one file, ResourceMaterializer.cpp, 2 logging-only hunks (TableCounters, reportTables). Both are mechanical; PM-028 needs no rework, because selected records return before its memo.
