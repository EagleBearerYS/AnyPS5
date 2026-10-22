READY

RESULT:
- last: PRM-056 done 2026-10-22T22:02:46Z (board 3ecd53ba3), result results/cloud-1/PRM-056.md (bea5af764).
- branch: rebase/7642-on-9508-5547aa1 = 9e843c1a9e, 8 commits on #9508's 5547aa11ef; no conflicts, no adaptations; range-diff 6 '=' and 2 '!' (context only).
- checks: 8 commits built and registered, table_trailer|device_image x5 green; the 8 reds red with BF-129's messages; hand-over x10 the same on head and #9508 (unwatched ok, watched case skipped 77 on lavapipe).
- mirror: head 1946/1953, #9508 alone 1939/1945; only-on-head failure agc_driver_pipelined_fill_gpu_heap is the WAIT_REG_MEM load flake, which also fails on #9508 alone (1/150 interleaved).
- previous: BF-153 done (010b46e4e).
- watcher running; no open tasks.
