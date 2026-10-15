READY
RESULT:
- Last task: GAC-026, done: results/cloud-1/GAC-026.md.
  - Branch: fork perf/agc-host-import-blocks f5bf721bc, 6 commits on upstream/main 598dd1595 (has 8856f180ca): step 0 requester tag d1c3a1a0f, shutdown counts 07779db2e, overhang test fix 9c1e05733, F1 aligned blocks d568e0f90, F6 refusal memory 0f85236fe, F3 device budget f5bf721bc.
  - Red/green: each new test red on the code before its fix and green after. F1: 64 → 1 import (map-time and build), holes 64 → 7, 48 unaligned pieces 48 → 5, 64 segment imports → 1 for a 4 MiB image. F6: "rebuild 1 tried 4800 refused ranges again" → 0. F3: 5 failures → 3 failures then 2 refusals; near the budget 3 → 1 then 2 refusals.
  - Step 2: F1 alone makes T3 green (1 import; 2 at +64 KiB), so no F2. T4 as written drops its store on both trees (memory not registered); a GPU-flag variant is kept as a guard.
  - Costs (30k 64 KiB ranges, lavapipe): importRun +111 ns per call; AcquireRegistered per rebuild 57.8 → 43.2 ms at a 60 % budget, 50.0 → 19.7 ms at 100 %; imports 18,000 → 5,318.
  - ^agc: 598/598 at F1, F6 and F3; 596/597 at step 0 and the test fix (scope_settle flake only). New tests 10/10 at every commit. Registration: none new.
  - Not built, with reasons: F7a (unsafe with in-flight old spaces; proposed), the maxMemoryAllocationCount cap (NVIDIA reports 4096; Village held 27,167 imports), F2/F4/F5 (PM's call). F3 recommended as its own PR after the first.
  - Conflicts: #6097, #5870 and #6174 clean. #6197, #2434 and #5905 have adjacent-line conflicts; #5372 and #6264 doc-only. Current main 92e50418f: debt-doc conflict only.
  - Addendum 08:26 PT (parl0v's announced #6060 follow-up), answered in the result:
    - Village's churn is a different mechanism.
    - Their sliding or growing single-range re-import is unchanged by this branch: a 32 MiB range sliding 2 MiB 4 times imports 108 MiB on main and here.
    - F1's block upgrades are their case, so the two compose. On the mock, a 4 MiB run grown piece by piece imports 16 MiB here against 4 MiB on main.
    - The overlap paragraph is written for the PR text.
    - Their branch is not public yet. The merge-tree is clean in importRunAt and ImportWatched.
- Task before it: PM-039, done: results/cloud-1/PM-039.md.
