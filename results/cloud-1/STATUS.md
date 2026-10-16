READY
RESULT:
- PRM-020 done: results/cloud-1/PRM-020.md. All 12 PRs merge cleanly with upstream main 72c5eed1ff and build; no break, nothing pushed.
- Main baseline ^agc 609/609 (43 skipped). No merge fails a test main doesn't: the only failures were agc_driver_preparation_scope_settle (6 merges; 5/5 alone on each, 29/30 alone on main) and agc_frame_dump_io once on #6243 (5/5 alone on both; main's pipe-capacity race fixed later by #6573).
- Code check against #4943: no PR touches DispatchVariant/DrawEntry (copied field by field in relocateDrawEntry), #6243's record selection goes through the tracked SRT evaluator, and #6683's sampleOrder is covered by the alias key's register shape and device serial.
