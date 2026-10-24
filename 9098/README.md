# #9098: the Resident Evil Village (PPSA01556) figures

One four-arm session on an RTX 3070 Laptop GPU (NVIDIA 617.42), Windows 11, with a warm shader cache. It ran with `APS5_LAZY_SHADER_PREPARE=1`, `APS5_NO_GPL=1` and `APS5_HOST_IMPORT_MIB=6400`, plus `APS5_TRACE_SUMMARY_EVERY=1` for per-second summary lines. GPU clocks weren't held.

**The arms:**
- A: `main` 1ff5c8ac5c plus the #6457 fix (tree d56bdc5155);
- B: A plus #9098's earlier head;
- C: A plus the new head's first three commits, without #9505;
- D: A plus #9505.

**What each run's folder (`q3b-<run>/`) holds:**
- `aps5-summary.jsonl`: one summary line per second;
- `launch.txt`: the launch time, in epoch seconds;
- `summary-stamps.txt`: the stamps that map the trace clock to wall time;
- `vm-queue-lines.txt`: the run-queue lines the script uses (Yes pressed, the dark load screen, the first lit frame).

`shutdown-imports.txt` has each run's `[gpu] host imports at shutdown` line. That line gives its import mode: low under 30,000 MiB made, high at 50,000 or more.

**How the figure is computed:**
- `q3b_report.py` takes the post-Yes dark phase, from the dark load screen to the first lit frame.
- Over that phase, it takes the presents-weighted mean of each interval's `top_phases_ms_per_frame["Driver.Packet.dispatch"]`.
- Its data paths read `<path>` here.
- **A caveat:** the summaries list only each interval's top eight phases, and the script counts a missing phase as 0.
- **The bounds:** `q3b_check.py` repeats the same selection and writes them to `q3b_check-output.txt`. One is the figure over the intervals that list dispatch. The other is an upper bound, with each missing interval set to its eighth phase.

| run | arm | mode (MiB made) | intervals without dispatch | dispatch, missing = 0 | over intervals that list it | upper bound | launch to first lit |
|---|---|---|---|---|---|---|---|
| a1 | A | high (137,003) | 8 of 302 | 1,111 | 1,123 | 1,191 | 42:11 |
| b1 | B | high (140,575) | 6 of 300 | 1,115 | 1,124 | 1,190 | 39:37 |
| b2r | B | high (136,413) | 11 of 300 | 1,055 | 1,071 | 1,191 | 45:50 |
| c1 | C | high (128,552) | 75 of 333 | 569 | 634 | 956 | 44:16 |
| a2r | A | low (15,371) | 121 of 351 | 285 | 324 | | 35:51 |
| a3 | A | low (13,878) | 5 of 323 | 681 | 684 | 736 | 39:45 |
| c2 | C | low (14,228) | 8 of 321 | 735 | 740 | 804 | 37:32 |
| d1 | D | low (13,578) | 7 of 331 | 800 | 806 | 872 | 41:05 |
| d2 | D | low (13,935) | 10 of 309 | 732 | 740 | 836 | 38:16 |

a2r is left out of the PR's figures: its load began from a menu-warmed state after a late Yes, a retry.

## Second session: B A A B load runs (q9098, 2026-10-24)

The same machine, driver and warm shader cache. The environment is the same as the first session's, with no trace directory and no other profiling switch. GPU clocks weren't held: nvidia-smi, sampled every 100 ms over each whole run, read P0 for 97.3 % (b1), 96.5 % (a1), 84.2 % (a2) and 98.2 % (b2) of the samples, and P3 or P5 for the rest.

**The arms:**
- A: tree d56bdc5155, as in the first session;
- B: tree 5ced89cd25, A plus #9098's earlier change 74dee0062c and its follow-up 156fa1f175.

**The route:** Continue, then Yes at the menu, and the run closes 180 s after the first lit frame. Times are Pacific:

| run | arm | launch | Yes | first lit | Yes to first lit | mode (MiB made) |
|---|---|---|---|---|---|---|
| b1 | B | 01:21:17 | 01:33:45 | 01:57:19 | 23:34 | low (13,912) |
| a1 | A | 02:00:53 | 02:15:46 | 02:43:32 | 27:46 | low (13,807) |
| a2 | A | 02:48:04 | 03:00:26 | 03:23:04 | 22:38 | high (135,861) |
| b2 | B | 03:26:54 | 03:37:46 | 04:04:06 | 26:20 | low (14,359) |

a2 ran in the high import mode, so it isn't compared with the low-mode runs.

**What each run's folder (`q9098-<run>/`) holds:** `aps5-summary.jsonl`, `summary-stamps.txt`, `launch.txt` and `shutdown-imports.txt`, as in the first session.

**The figure:** `q9098_check.py` takes the summary lines whose `first_present` lies between the present at Yes and the present at the first lit frame. That window starts at Yes, 13-23 s before the dark load screen where the first session's window starts. Over it, the script takes the presents-weighted mean of `Driver.Packet.dispatch`, as `q3b_report.py` does, with a missing interval counted as 0 and then set to its eighth phase. Its output is in `q9098_check-output.txt`, which also lists the median per line.

| run | arm | intervals without dispatch | dispatch, missing = 0 | upper bound |
|---|---|---|---|---|
| b1 | B | 11 of 334 | 668 | 748 |
| a1 | A | 9 of 404 | 827 | 904 |
| a2 | A (high) | 7 of 292 | 1,039 | 1,120 |
| b2 | B | 10 of 336 | 771 | 854 |
