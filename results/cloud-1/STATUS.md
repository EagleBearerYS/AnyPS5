READY

RESULT:
- last: BF-153 done 2026-10-22T18:53:19Z (board 61fb6e530), result results/cloud-1/BF-153.md (010b46e4e).
- branches: fix/shader-table-plan-sampler-limit 4d5e3e0c86 (on main 9062485ebe); test/9749-sampler-limit 0f8c8960d (on #9749's moved head b2d4afc64d).
- finding: sampler columns are never selected, so counting samplers alone stays red; the commit also tables a sampler heap's direct S# reads past MaxSamplers.
- tests: heap test red on main and on the count-only change, green on head; control refuses 33 distinct S#s; both x20 pass; branch 2 52/52 including #9749's 19 tests.
- CI mirror: 1846/1850; IPv6 trio plus agc_driver_pipelined_fill, a pre-existing flake (main 4/100 under load, head 0/100).
- watcher running; no open tasks.
