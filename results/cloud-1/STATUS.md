READY
RESULT:
- Last task: PM-039, done: results/cloud-1/PM-039.md.
  - Branch: fork followup/6146-completeness a6005bab0, 9 commits on 2697ae5d9, Addendum 2's base. Gap 2 builds on its drain, and its three queued cases stay green. My own duplicate queued case was dropped.
  - Fixed: gap 2 (f2f52d08a), gap 5 (a2339f04c), gap 4 D16/slices/stencil (7c485c08e, 915a71718, c32ed2ac9), gap 7 (67a68a167), gap 10 (dc4557ffa), gap 8 tests (704020d28) and gap 11 docs (a6005bab0).
  - Red/green: each behaviour fix is red on the pushed commit with the fix reverted, and green per commit. Gap 7 has no red that can be reproduced on lavapipe, and the result says why; its regression case is green.
  - Scope, with reasons: gaps 3 (DB_RENDER_OVERRIDE copies; #1395 overlap), 6 (in-place expand), 9, 12, 13 and 14.
  - CI mirror per commit: configure, registration, build and libs exit 0, and 25/25 or 26/26 named tests at each commit.
  - Head, fresh build: 1245 tests; 5 failed, the socket baseline. check_skips flags agc_import_watch_other_family and guest_sce_net, both known.
  - ^agc: 592 tests. The only failure is agc_driver_preparation_scope_settle, the base's load-sensitive flake: head failed in 4 of 6 runs and passed 10/10 alone; base 2697ae5d9 failed in 3 of 7.
  - Expand tests: 5/5 runs at the pipeline default and 5/5 with APS5_PIPELINED_DRAWS=0. Conventions pass against upstream/main, 8856f180ca and 2697ae5d9.
  - Merge-tree: clean with main 8856f180ca and with current main 34454bd835. Against #6188 and #5925, the only conflict beyond 2697ae5d9's is DrawPrecheck.cpp, and that is intended.
- Task before it: BF-011, done: results/cloud-1/BF-011.md. Verdict GREEN: only the baseline fails.
