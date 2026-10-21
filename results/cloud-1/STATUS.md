READY

RESULT:
- task: BF-116 done, result results/cloud-1/BF-116.md
- branch: fork fix/agc-draw-aliases-by-key-test = 1d2fdfb261 (fix 41196116ee + test commit)
- test: agc_driver_draw_cache_relocation_evicted (--evicted, APS5_DRAW_CACHE_ENTRIES=2)
- red (fix reverted): "a draw whose data moved reused a draw the cache had evicted, through its relocation alias (1 hits)"
- green: 10/10 for each of the 2 relocation ctests; ^agc_driver_draw_cache 6/6; registration none new; conventions passed
