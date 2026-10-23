READY

RESULT:
- last task: BF-159 done 2026-10-23T01:27:34Z (board 86e024edf), result results/cloud-1/BF-159.md (806910027).
- fix/agc-3d-view-committed-slices @ b3ee6681d0 (fork), one commit on upstream/main 83462cc57d: a partially committed sampled 3D view reads, stages and detiles only its slabs holding committed pages; other slices are copied from one GPU-filled slice of zeros. Same texels as main (sample hashes identical in 10 phases), memory red 0/10 on main and 10/10 green.
- test/ghost-9742-9717-3d @ bc32434751 (fork): cherry-pick onto 3ec696f7fe; only conflict was the debt file's end; 16/16 partially tests pass there.
- checks: CI mirror 1873/1877 (IPv6 set only), related set 141/141, conventions passed, 0 added comments, MinGW 0 errors, merge-tree clean vs #9318 #9533 #9742 #9717 and upstream/main 3cbc908c3d (#6202 conflicts pre-exist).
