# PRM-104c: #7642: merge #9508's newest head into PRM-104b's b5065a1fe9: #9508's new commits and main's #12862 split in one merge, restoring the 22 `_gpu_heap` tests

from: AnyPS5 PR Manager
for: any
priority: high

## Context

- **PRM-104b** (`results/cloud-2/PRM-104b.md`) merged #9508's 31f5d0e8dd into #7642's 06eba99c50 as **b5065a1fe9**, on fork `prm/7642-9508-31f5d0e8dd`. Read it first.
  - It made two named edits: `HeapRetention` drops the deleted sweep calls, and `HeapSlots` passes `texture.get()`.
  - It kept both sides of `ARCHITECTURE.md`.
  - All its runs passed.
- **#9508 has moved since.** At 00:22Z on 11-01 its head is **f2449f6ec2** (`f2449f6ec25d5c1f7e61839e34f02daa12223293`), which contains 31f5d0e8dd (136 ahead, 0 behind). On top of 31f5d0e8dd it adds:
  - 866bd70c70 "refactor(shader): tidy the GPU heap recompiler paths";
  - 11ef9c57b1 "let one draw's shader data span several heap ring chunks";
  - dfd8a5eff9 "never wrap a heap ring span onto the current chunk behind its cursor";
  - two merges of `main`, 7b7d146541 (0836dfdf4f) and 936b81773f (8a4fc62318);
  - f2449f6ec2 "create the GPU heap before the memory accounting baseline".
- **Use #9508's head when you start.** Fetch `upstream` `+refs/pull/9508/head` and give its SHA. If it isn't f2449f6ec2 or a descendant of it, stop. If it moves while you work, finish on the one you started with and say so.
- **#7642 is upstream's `refs/pull/7642/head` = 06eba99c50.** Stop if it differs.

## What to do

1. **Merge:** on b5065a1fe9, `git -c rerere.enabled=false merge --no-ff --no-commit <#9508's head>`.
2. **Expected conflicts:**
   - `core/libs/prx/libSceAgcDriver/CMakeLists.txt`, from #12862's split, which #9508's main merges bring;
   - possibly `tests/cmake/agc_driver_address_draw_space_reuse_tests.cmake` and other fragments, if #9508 registers there.
   Run `python3 tools/agc_test_split.py resolve` (the merged tree's copy). It must exit 0, and leave no unmerged file and no conflict marker.
3. **The one named edit: restore the 22 `_gpu_heap` variants.** After `resolve`, #7642's GPU-heap block still reads `set(agcGpuHeapTests ${agcDrawPipelineTests} agc_driver_rect_list_indirect …)`. But #12862 removed `agcDrawPipelineTests`, so the list expands empty and the 22 variants vanish with no warning (BF-349 §2.4).
   - Replace `${agcDrawPipelineTests}` with the 22 names that #12862's 22 `agc_add_draw_pipeline_test` calls register, in the same order.
   - The resulting `_gpu_heap` tests are exactly BF-349 §2.4's list: `agc_driver_bda_span_reads_gpu_heap` … `agc_driver_write_data_written_ahead_gpu_heap`.
   - Nothing else in that block changes. If it needs more, such as a new fragment or another variable, stop and report.
4. **Every other conflict is a stop,** before you commit anything:
   - any file outside the AGC CMake files and their fragments;
   - #9508's 866bd70c70 colliding with #7642's recompiler changes;
   - any need to touch the PRM-104b edits (`HeapRetention`, `texture.get()`, the `ARCHITECTURE.md` line) again.
   Report the hunks. If git conflicts in a shader-cache `FormatVersion`, report both values and stop.
5. **The semantic check, read at the merge, with `file:line`:** 11ef9c57b1 lets one draw's shader data span several heap ring chunks. Does any of #7642's code assume a draw's heap-ring data sits in one chunk? Look at where it writes into the heap ring or reads back from it: the device image table's trailer words, the bucket and miss ring, and the slot uploads. That means a single base address plus an offset, or a size check against one chunk.
   - **If it does, or if you can't tell:** stop, make no code change, and report the lines.
   - If it doesn't, give the lines that show why.
6. **Commit**, as `Yunus Shaikh <75296019+EagleBearerYS@users.noreply.github.com>` for author and committer, unsigned, with no trailers:
   ```
   Merge #9508's <short sha> into feat/agc-device-image-table

   This brings #9508's commits since 31f5d0e8dd and its merges of main, which carry #12862's test split.

   The conflict in core/libs/prx/libSceAgcDriver/CMakeLists.txt is resolved with tools/agc_test_split.py resolve, which moves this PR's test registrations into tests/cmake/. One edit beyond that: the GPU heap test list named ${agcDrawPipelineTests}, which #12862 removed, so the list now names the 22 draw pipeline tests itself and their _gpu_heap variants stay registered.
   ```
   Push it to fork `prm/7642-9508-<short sha>`.

## Checks (stop on any miss)

- **Nothing else changed:** `git merge-tree --write-tree b5065a1fe9 <#9508 head>` gives git's own merge. The diff against yours may list only `libSceAgcDriver/CMakeLists.txt` and `libSceAgcDriver/tests/cmake/*.cmake`.
- **The count.** Configure three trees with CI's `CONFIGURE` flags plus `-DANYPS5_ENABLE_SPIRV_TOOLS=ON`, and compare their `ctest --show-only=json-v1` lists:
  - (a) #9508's head;
  - (b) b5065a1fe9;
  - (c) the new merge.
  The rules:
  - every name in (a) must be in (c);
  - every name in (b) must be in (c), unless #9508 or `main` removed that test between, with the commit named;
  - the 22 `_gpu_heap` variants must be in (c);
  - no name appears twice;
  - report the three totals and any name only in (b) or only in (c) beyond (a) ∪ (b).
- **Own diff:** compare the sorted `+`/`−` lines of `git diff <#9508 head> <new>` with `git diff 31f5d0e8dd b5065a1fe9`. The expected differences are:
  - the registration move;
  - the 22-name edit;
  - lines #9508 itself changed underneath (#7642's lines are unchanged, only their context).
  List them all.
- **Builds and runs** (CI's Linux flags, `-DANYPS5_ENABLE_SPIRV_TOOLS=ON`, lavapipe, as `runner`, `ANYPS5_REQUIRE_VULKAN=1 GALLIUM_OVERRIDE_CPU_CAPS=avx`):
  - the full build, exit 0;
  - #7642's set ×10, as in PRM-104b, plus the 22 restored `_gpu_heap` tests (they skip without `VK_EXT_descriptor_heap`; give the skip reason);
  - every test from the files 31f5d0e8dd..<#9508 head> changes, ×10, mapped as PRM-104b did;
  - `^(agc|shader)` once;
  - `tools/check_test_registration.py build` and `tools/check_conventions.py --base <#9508 head>`.
- **Identity** and **0 added comment lines** (`git diff <#9508 head> <new> | grep -cE '^\+\s*//'`).

## Rules

- No PR branch is touched, and nothing is posted.
- Upstream refs come from the `upstream` remote. Push only the new fork branch.
- No comments in code.

## Result

`results/cloud-<n>/PRM-104c.md`, with:
- #9508's head used, and the merge SHA;
- each conflict and its resolution;
- the 22-name edit as a diff;
- the semantic check with lines;
- the three totals and the name lists;
- the own-diff differences;
- every run's summary line, with skip reasons;
- the checks' lines and the identity.

When you finish, write a `done:` line in the task file, then claim the next open task or restart the board watcher.

## When you finish

After you mark this task done, claim the next open task on the board right away (priority high first, then the lowest id; skip any whose `for:` line names another worker). If none is open, restart the board watcher as a background command before you end your turn. A worker that ends its turn with no task and no watcher is never woken: on 2026-10-15 one sat READY for 30 minutes beside two open tasks.
