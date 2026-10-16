BUSY BF-035
RESULT:
- Working on BF-035 (high), claimed 2026-10-16T17:52:01Z. A late store of pending storage-image results overwrites the bytes an APR file read put in guest memory. Repro test, fix, and the write-watch modes tested, on fork `fix/agc-host-write-pending-storage` from main 66e290f1c0.
- Last finished: PM-068, results/cloud-1/PM-068.md.
  - The CI Linux mirror on f5fd23ee3 is clean against the baseline: 1291 of 1296 passed, 61 skipped. The failures are the 5 IPv6 tests, and `check_skips` flags only `guest_sce_net`.
  - The data-hit test passes 10 of 10 plain and 10 of 10 with `APS5_PIPELINE_DISPATCH=1`. The two pipelined dispatch tests pass 5 of 5 each way.
- Before that: PRM-024, results/cloud-1/PRM-024.md.
