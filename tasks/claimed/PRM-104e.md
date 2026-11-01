# PRM-104e: #7642: merge #9508's 09cc865e29 (or a descendant) into b5065a1fe9, with PRM-104c's five resolutions and PRM-104d's two edits

from: AnyPS5 PR Manager
for: any
priority: high

## Context

- **PRM-104c** stopped on five C++ hunks. Its §2 is the text: `results/cloud-3/PRM-104c.md`.
- **PRM-104d** stopped because #9508 moved to **09cc865e29** (`09cc865e29c365a5b8152d68f2fb8f8d3f963872`). Its dry run found two more things: `results/cloud-3/PRM-104d.md`, §2 and §3. Read both results first.
- **The decision (the user's OK, 11-01):** resolve them as those results propose, as listed below, each named in the merge message.
- **Unchanged from PRM-104c:** `resolve` for `CMakeLists.txt`, the count, the checks and the result format.
- **Also kept from PRM-104d:** step 3's 22-name edit stays dropped, and `FormatVersion` stays 62.

## Start: the head rule

1. Fetch `refs/pull/9508/head` and `refs/pull/7642/head` from `upstream`. #7642 must be 06eba99c50; stop if not.
2. **Use #9508's head as it is at your start, if both hold:**
   - it is 09cc865e29 or descends from it (`git merge-base --is-ancestor 09cc865e29 <head>`);
   - `git merge-tree --write-tree b5065a1fe9 <head>` gives exactly PRM-104d's conflicts: the three `CMakeLists.txt` hunks, PRM-104c's five C++ hunks byte for byte, and the one `tests/BindlessHeapSlots.cpp` hunk.
   **Anything else is a stop:** another conflicted file, a hunk that differs, or a head not descending from 09cc865e29. Report it and commit nothing.
3. If #9508 moves after you start, finish on the head you started with, and say so.
4. Merge: `git checkout --detach b5065a1fe9`, then `git -c rerere.enabled=false merge --no-ff --no-commit <#9508 head>`, then `python3 tools/agc_test_split.py resolve`.
   - **If you are cloud-3:** PRM-104c's uncommitted merge is of 37c94cd099. Abort it and start fresh.

## The resolutions (and nothing else)

1. **The five hunks of PRM-104c §2**, as its "Proposed" blocks, in `ShaderDiskCache.cpp` (encode and decode), `Dispatch.cpp`, `DrawCapture.cpp` and `ShaderResources.cpp`.
2. **`tests/BindlessHeapSlots.cpp` (PRM-104d §2):** #9508's helpers in #7642's order. That is `SetTestEnvironment("APS5_GPU_HEAP", "1");` and `SetTestEnvironment("APS5_HEAP_SLOT_CAP", comesBack ? "8" : "1");`, then `auto device = OpenVulkanTestDevice();`, then the null-device and `graphicsContext().descriptorHeap` checks, exactly as PRM-104d §2's "Proposed" block.
3. **`SpirvDescriptors.cpp`, `TableTrailerWord` (PRM-104d §3):** its two `tableMapWord(...)` calls become `LoadTableMap(...)`, with the same arguments, exactly as PRM-104d §3's block. Nothing else in the file changes.
   - Then `git grep -n tableMapWord` on the merged tree must find nothing.

## Commit

As `Yunus Shaikh <75296019+EagleBearerYS@users.noreply.github.com>` for author and committer, unsigned, with no trailers:
```
Merge #9508's <short sha> into feat/agc-device-image-table

This brings #9508's commits since 31f5d0e8dd and its merges of main, which carry #12862's test split.

The conflict in core/libs/prx/libSceAgcDriver/CMakeLists.txt is resolved with tools/agc_test_split.py resolve, which moves this PR's test registrations into tests/cmake/. #9508's split merge already registers the _gpu_heap variants through agc_add_gpu_heap_test(), so this PR's GPU heap test list needs no edit.

The other conflicts keep both sides:
- ShaderDiskCache.cpp: #9508's 866bd70c70 drops the second encode and decode of userDataRegisterReads, and this PR's device image interface and sampler class lists follow imageTableFaults in both.
- Dispatch.cpp and DrawCapture.cpp: this PR's DeviceImagesServe line, then #9508's 675e23803d heap key and profile lines.
- ShaderResources.cpp: this PR's DeviceImageTable.hpp include and #10194's MemoryAccounting.hpp include.
- tests/BindlessHeapSlots.cpp: #9508's SetTestEnvironment and OpenVulkanTestDevice helpers, with both settings made before the device opens, as this PR's 0f3e74aa41 does, since BindlessHeap reads APS5_HEAP_SLOT_CAP once, in its constructor.

#9508's 0f4b636d04 replaces SpirvDescriptors.cpp's tableMapWord with the shared LoadTableMap, so this PR's TableTrailerWord now calls LoadTableMap too.
```
Push to fork `prm/7642-9508-<short sha>`.

## Checks and runs

All of PRM-104c's checks, builds, runs, identity and comment-line checks, with these specifics:
- **Nothing else changed:** the diff against `git merge-tree --write-tree b5065a1fe9 <#9508 head>`'s tree may list only:
  - `libSceAgcDriver/CMakeLists.txt` and `tests/cmake/*.cmake`;
  - the four PRM-104c C++ files, in them only those hunks;
  - `tests/BindlessHeapSlots.cpp`, only that hunk;
  - `SpirvDescriptors.cpp`, only the two calls.
- **The count:** configure (a) #9508's head and (c) the merge. (b) b5065a1fe9 stays PRM-104c's 3,193. The rules:
  - (c) = (a) ∪ (b);
  - no duplicates;
  - all 22 `_gpu_heap` variants present.
  Report the totals.
- **The runs:**
  - #7642's set ×10, as in PRM-104b;
  - every test from the files 31f5d0e8dd..<#9508 head> changes ×10, mapped as PRM-104b did. That includes by name `agc_shader_disk_cache`, `agc_shader_memory`, the tests of `tests/BindlessHeapSlots.cpp` (map the file to its `add_test` names), and `agc_shader_table_trailer`;
  - `^(agc|shader)` once.
- **The `LoadTableMap` branch:** its non-ring branch, the table-map buffer, is the heap path, which needs `VK_EXT_descriptor_heap`. lavapipe lacks it.
  - From the logs, say which of the tests above reach a shader that reads the trailer through that branch.
  - If every such test skips (give the skip line), say so plainly: the trailer read through that branch isn't run on this machine.
  - If any test does reach it, name it and give its result.
- **The disk-cache round trip:** `agc_shader_disk_cache` must pass. If no case writes and reads back an invocation with non-empty `deviceImageInterfaces` and `deviceSamplerClasses`, say so; don't add one.

## Result

`results/cloud-<n>/PRM-104e.md`:
- the heads used and the merge SHA;
- each resolution as committed;
- the count;
- every run's summary line, with skip reasons;
- the `LoadTableMap` branch answer;
- the checks and the identity.

No PR branch is touched, and nothing is posted.

## When you finish

After you mark this task done, claim the next open task on the board right away (priority high first, then the lowest id; skip any whose `for:` line names another worker). If none is open, restart the board watcher as a background command before you end your turn. A worker that ends its turn with no task and no watcher is never woken: on 2026-10-15 one sat READY for 30 minutes beside two open tasks.
