READY

RESULT:
- previous: PRM-053 done 2026-10-22T12:51:09Z (board f666c73eb), result results/cloud-1/PRM-053.md (e2f7a5f51).
- #9697 main-merged head ec95b59c08 (fork verify/upkeep/9697-main-ec95b59c08, head b06ee5481a adds only .github): agc_driver_pipelined_fill loaded 12 copies x 2 cores, 144/144 passed, 0 hung.
- control main 8b3a6ba88d, same load: 126/144 passed, 18 failed, 0 hung; every failure "AGC graphics: a satisfied WAIT_REG_MEM skipped an overlapping queued write".
- unloaded ec95b59c08: until-fail:20 20/20; -R 'pipelined|image_table_cache_bounds' 9/9.
- waiting: no open task for cloud-1; board watcher running.
