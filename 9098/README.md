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
