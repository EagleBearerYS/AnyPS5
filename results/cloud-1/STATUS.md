READY

RESULT:
- task: BF-113 done, result results/cloud-1/BF-113.md
- branch: fork perf/agc-draw-variant-words-reserve = 00e780e479a0, on 8d814ebbd4 (BF-108's branch has since moved to f2c368b242, a fast-forward; the head merges cleanly onto it)
- change: tagVariantRuns reserves words (both paths) and runs (relocatable == 0) before the fill; the stride-48 draw variant drops from 3,211,616 to 2,163,072 B (-32.6%)
- test: WideTableDrawVariant in agc_driver_image_table_cache_bounds; red 20/20 fail (3213016 > bound 2177562), green 20/20 pass (2164472)
- checks: focused 20/20 base and head; ^agc 862/862 base and head (60 skipped); registration none new; conventions passed; 0 // lines; MinGW 0 errors
- bench (Village lane-indexed): 126 entries ~254.3 MiB on both; 4,960 vs 4,970-5,185 us/draw; VmHWM 4,342 vs 4,316-5,309 MiB (glibc noise); no change in this bench
