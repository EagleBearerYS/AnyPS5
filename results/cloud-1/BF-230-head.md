# BF-230 early head (cloud-1)

The PC can build its Village trees from this head now. The rest of BF-230 follows in `results/cloud-1/BF-230.md`.

- **Head:** fork `perf/agc-template-write-index-v2` = **a1345c6917** (`a1345c6917a786c3d1a48135a87d65dfc0afdcbe`), pushed 2026-10-25T13:50Z.
- **Base:** upstream main **85a4d7f8c8** (`85a4d7f8c82cf3feeed07f59e7732218958db890`, "Merge pull request #10754"), fetched 13:39Z. That is 3,024 commits past BF-136's 17f5335562, and past the task's 1ee8f4bec1.
- **Kept for comparison:** `perf/agc-template-write-index` = 9ccbfe55a7, unchanged.

**The four commits,** BF-136's in the same order, not squashed:

| commit | subject | was |
|---|---|---|
| a5032863b2 | perf(agc): prove cached image tables from a write index behind APS5_TABLE_INDEX | 1c43651778 |
| 32f715b3fb | test(agc): check the write index's invalidation sources | 81f51a8647 |
| c8461c3f14 | test(agc): measure per-draw cost on a Village-shaped frame with a bindless T# table | 12cc43e84d |
| a1345c6917 | test(agc): make the Village harness's table mode templatable | 9ccbfe55a7 |

- **Identity:** author and committer Yunus Shaikh <75296019+EagleBearerYS@users.noreply.github.com>, unsigned, no trailers. Commit 1 keeps its "Port of BF-041 (0063b98265)" line.

**Conflicts,** all in commit 1, all resolved by keeping both sides:
- `Execution/include/GuestMemory.hpp`: main's `Mutex.hpp` include and the index's `<array>`.
- `Graphics/include/ShaderResources.hpp`: the index's `WriteIndexOutcomes` declarations, with main's `ReleaseTextureCaches(bytes, newImage = false)` from #7800.
- `Graphics/src/ShaderResources.cpp`: main's `EpochProofs()` and `noteEpochProof` (#6946) and the index's block, each in its own anonymous namespace.
  - The index's `Revalidate` hunks merged cleanly into main's `revalidateFull`, the body #6946 split out of `Revalidate`.

**Adaptations, in commit 1:**
- **#8895:** the index calls main's `AnyPendingCpuWrites` and drops its identical private `AnyPendingCpuWriters`.
- **#6292:** the index's lock is now `AgcDriver::Mutex`, like `GuestMemory`'s own locks.
  - Its ten `thread_local` scratch vectors (all in `ShaderResources.cpp`) already use main's `ShaderRecompiler::ThreadOwned` slots, so they are unchanged.
  - `GuestMemory.cpp` gains no `thread_local`.

**Relinker:** no source under `core/relinker` changed (`git diff --name-only upstream/main...a1345c6917 -- core/relinker` is empty). The branch changes 10 files, all under `core/libs/prx/libSceAgcDriver` and `docs/dev/ENVIRONMENT.md`.

**Build:**
- **Configuration:** CI's configuration (the `ci-linux.sh` configure already in `build/`), with an incremental build of the index's targets: exit 0, 510 s.
- **Warnings:** 6, all in files the branch does not touch (`DepthSurface.cpp:1212`, `Signals.cpp:24`, and 4 in system or libstdc++ headers).

**Runs on a1345c6917:**
- **How:** each test alone, `ctest -R '^<name>$'`, as the unprivileged `runner` user, with `ANYPS5_REQUIRE_VULKAN=1 GALLIUM_OVERRIDE_CPU_CAPS=avx` on lavapipe.
- **The switch:** off means `APS5_TABLE_INDEX` unset; on means `APS5_TABLE_INDEX=1`.

| test | switch off | switch on |
|---|---|---|
| `agc_driver_table_index_validation` (sets the switch itself) | 5 passed, 0 skipped, 0 failed of 5 | 5 passed, 0 skipped, 0 failed of 5 |
| `agc_draw_descriptors` | 5 passed, 0 skipped, 0 failed of 5 | 5 passed, 0 skipped, 0 failed of 5 |
| `agc_driver_lookup_log_fits_build` (pins the switch off itself) | 5 passed, 0 skipped, 0 failed of 5 | 5 passed, 0 skipped, 0 failed of 5 |

**Checks:**
- `check_conventions.py --base upstream/main`: "All conventions checks passed."
- 0 added comment lines, and `git diff --check` is clean.
