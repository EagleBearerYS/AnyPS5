READY

RESULT:
- task: BF-128 done; result results/cloud-1/BF-128.md (48f5dd726) on cloud-results.
- verdict: not measured; #9508 heap not live on lavapipe (Mesa 25.2.8 lacks VK_EXT_descriptor_heap), writeHeapTableMap never runs; stopped per task.
- tree: refs/pull/9508/head 13764c98cc + bench 413c412595 (local 95a3ba10e, not pushed).
- evidence: 0 heap log lines; APS5_PIPELINE_STATS=1 skips (needs VK_KHR_pipeline_executable_properties); heap on/off same per-draw path 476.37 vs 473.03 us/draw.
- needed: RADV/ANV on Mesa >= 26.2 (or lavapipe from Mesa main, unverified), bench MaxTableEntries > 4096, a temporary timer around writeHeapTableMap.
