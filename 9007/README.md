# #9007: the Resident Evil Village (PPSA01556) memory figures

Two menu sessions on Windows, an RTX 3070 Laptop GPU (NVIDIA 617.42), with graphics pipeline libraries on, a warm shader cache, `APS5_LAZY_SHADER_PREPARE=1`, `APS5_HOST_IMPORT_MIB=6400` and `APS5_TRACE_SUMMARY_EVERY=1`. GPU clocks weren't held. Each session ran A B B A, with the game held at its menu.

**The arms:**
- A: tree a1f0750708, a build with library and pipeline counters;
- B: tree 6ed8ff9aae, A plus this PR's one commit.

**The sessions:**
- 6f (`q6f-*`), 2026-10-23, 21:57 to 23:4x PT;
- 6f2 (`q6ft-*`), 2026-10-24, 04:23 to 06:15 PT, with `APS5_TRACE_SECONDS=1500:60` as well. The trace window fell after the menu + 10 minute point.

**What each run's folder holds:**
- `mem10.txt`: the process's private memory, working set and the page file allocation, sampled every 10 to 15 s;
- `menu-seen.txt`: when the run's menu screenshot was written.

**How the figures are computed:** `pool_memory.py` takes the sample nearest the menu + 1 and + 10 minute points, the peak private memory and the largest page file allocation. It then compares each B run with the A run next to it (b1 with a1, b2 with a2). Its output is in `pool_memory-output.txt`.

| session | run | arm | menu + 1 | menu + 10 | peak | page file |
|---|---|---|---|---|---|---|
| 6f | a1 | A | 24,165 | 29,504 | 30,030 | 32,768 |
| 6f | b1 | B | 23,400 | 29,510 | 30,286 | 32,768 |
| 6f | b2 | B | 22,869 | 28,204 | 29,476 | 32,768 |
| 6f | a2 | A | 23,971 | 29,373 | 30,077 | 32,768 |
| 6f2 | a1 | A | 23,414 | 28,372 | 29,210 | 33,798 |
| 6f2 | b1 | B | 22,328 | 27,399 | 28,531 | 32,768 |
| 6f2 | b2 | B | 22,141 | 27,671 | 29,070 | 32,768 |
| 6f2 | a2 | A | 24,086 | 29,219 | 30,826 | 34,830 |

All figures are MB. The Village agent's own 6f2 figures, taken at its own menu times, differ from these by up to 0.8 GB at menu + 1, where private memory climbs fastest, and by under 0.2 GB elsewhere.

## Traces (6f2)

`traces/` holds the four 6f2 runs' traces (`APS5_TRACE_SECONDS=1500:60`, about menu + 15 to 18 minutes), the `aps5-summary.jsonl` line over each traced window (`q6ft-<run>-summary.json`) and `tools/trace_digest.py`'s output for each (`q6ft-<run>-digest.txt`). a1 and a2 are A (a1f0750708), b1 and b2 are B (6ed8ff9aae).
