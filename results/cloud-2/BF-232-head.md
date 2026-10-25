# BF-232 part 1 head (cloud-2)

**Ready for the profiled Village look:** fork `diag/agc-collect-failures-v2` = **a9f338d3da**, three profile-only commits on upstream `main` **e05ba6feeb** ("Merge pull request #10596", fetched 2026-10-25T13:42Z; main has moved past 1ee8f4bec1).

| commit | from | subject |
|---|---|---|
| a6082189c3 | 792cee8300 | chore(agc): time image tables apart in the build and revalidate profiles |
| 4519427882 | 7d16b57df8 | chore(agc): time queue 0's drains and indirect reads apart in the draw profile |
| a9f338d3da | 404462a89c | chore(agc): count failed write-watch collects by reason in the memory profile |

**How it was checked:**
- **Build:** CI's configure flags (Ninja, Release, gcc 13.3, ccache, wild linker). I built the `libs` target and every executable the `^agc` tests run: 661 targets, rc 0. None of the 19 warnings is in a changed file.
- **Tests:** `ctest -R '^agc' --timeout 120 --parallel 4` as `runner`: 1010 tests, **945 passed, 65 skipped, 0 failed**.
- **Conventions:** `check_conventions.py --base upstream/main` passed, with 0 added comment lines.
- **MinGW** (`x86_64-w64-mingw32-g++-posix` 13, `-fsyntax-only -Wall -Wextra`): `ShaderResources.cpp`, `Draw.cpp` and `GuestMemory.cpp` give the same diagnostics as main's copies, ignoring line numbers: 127, 57 and 2 warnings.
  - There is one error in both: `libc/include/CpuTopology.hpp:93`, `PROCESSOR_RELATIONSHIP` has no `EfficiencyClass`. This VM's mingw-w64 11 headers predate that field; WinLibs on the PC has it.

**Output:** only under `APS5_PROFILE_DRAW`, as before.
- `[guestmem]` gains ` | collect failures (10 s): unwatched N outside N incomplete N, ranges: <reason> 0x<addr>+0x<bytes> x<count> …` (up to 12 ranges).
- `[resources]` gains the "tables" and "table finish" phases.
- `[rescache] revalidate` gains "(full walks X ms, their image tables Y ms)".
- `[draw]` gains "drain wait" and "indirect read".

**Reading the counts:** the collect-failure totals count every caller of `CollectWrites`, including the full walk's own collects, not just the fast proof. To tie them to the 414 `fast-fail by reason: collect` per 10 s, use the sample ranges: the ones repeating at the proof rate are the elements failing the proof.

The full write-up (adaptations, part 2) follows in `results/cloud-2/BF-232.md`.
