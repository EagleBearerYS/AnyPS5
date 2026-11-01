# PRM-104d: #7642: finish PRM-104c's merge of #9508's 37c94cd099 into b5065a1fe9, with its five named resolutions

from: AnyPS5 PR Manager
for: any
priority: high

## Context

- **PRM-104c** (`results/cloud-3/PRM-104c.md`) stopped before committing. It hit five C++ hunks in four files, merging #9508's **37c94cd099** (`37c94cd0991a8e417d054cf38123373c442c2563`) into PRM-104b's **b5065a1fe9**. Read it first: §2 has every hunk.
- **The decision (the user's OK, 11-01):** resolve the five exactly as PRM-104c §2 proposes. In each, #7642's side is the base plus added lines, and #9508's side changes base lines, so both are kept.
- **Everything else in PRM-104c stands:**
  - the `resolve` run for `CMakeLists.txt`;
  - the semantic check, which it already passed;
  - the count;
  - the checks, builds, runs, identity and result format.
- **Two changes from PRM-104c:**
  - step 3's 22-name edit is dropped, since #9508's split merge registers every `_gpu_heap` variant through `agc_add_gpu_heap_test()`;
  - `FormatVersion` stays 62, as git merges it.

## Start

- **If you are cloud-3 and still have PRM-104c's uncommitted merge:** check that it's the same merge, then go on from it.
  - `git rev-parse MERGE_HEAD` is 37c94cd099 and `HEAD` is b5065a1fe9.
  - `git diff --name-only --diff-filter=U` lists only the four C++ files, once `resolve` has run.
- **Otherwise:** redo PRM-104c's §1 command on b5065a1fe9 (fork `prm/7642-9508-31f5d0e8dd`) with 37c94cd099, then run `python3 tools/agc_test_split.py resolve`.
- **Upstream heads:** fetch `refs/pull/9508/head` and `refs/pull/7642/head` from `upstream`.
  - **If #9508's head is no longer 37c94cd099 when you start, stop:** report the new head and commit nothing. If it moves after you start, finish on 37c94cd099 and say so.
  - If #7642's isn't 06eba99c50, stop.

## The five resolutions (and nothing else)

The texts are PRM-104c §2's "Proposed" blocks:
1. **`core/shader/recompiler/ShaderDiskCache.cpp`, `encodeInvocation`:**
   - 866bd70c70's removal of the second `writer.Value(invocation.userDataRegisterReads);` stays;
   - #7642's `writer.List(invocation.deviceImageInterfaces, …)` and `writer.List(invocation.deviceSamplerClasses, …)` follow `writer.Value(invocation.imageTableFaults);`.
2. **The same file, `decodeInvocation`:** the same, with `reader.Value`/`reader.List`. Encode and decode must stay symmetric.
3. **`Execution/src/Driver/Dispatch/Dispatch.cpp`:**
   - first #7642's `if (request.target.deviceImageTables) request.target.deviceImageTables = localDevice->DeviceImagesServe(address);`;
   - then #9508's `const std::size_t computeHeapKey = HeapFragmentKey(codeOffset, HeapKeyClass::Compute);` and its `gpuResolvedImages` line.
4. **`Execution/src/Driver/Draw/DrawCapture.cpp`:**
   - first #7642's `DeviceImagesServe(program.binary.codeAddress)` line;
   - then #9508's `waitedBefore` line with `context.profile`.
5. **`Graphics/src/ShaderResources.cpp`:** both includes, `DeviceImageTable.hpp` then `MemoryAccounting.hpp`, after `BufferPool.hpp` and before `Pipeline.hpp`.

**A conflict anywhere else, or a hunk that differs from PRM-104c §2, is a stop:** report it and commit nothing.

## Commit

As `Yunus Shaikh <75296019+EagleBearerYS@users.noreply.github.com>` for author and committer, unsigned, with no trailers:
```
Merge #9508's 37c94cd099 into feat/agc-device-image-table

This brings #9508's commits since 31f5d0e8dd and its merges of main, which carry #12862's test split.

The conflict in core/libs/prx/libSceAgcDriver/CMakeLists.txt is resolved with tools/agc_test_split.py resolve, which moves this PR's test registrations into tests/cmake/. #9508's split merge already registers the _gpu_heap variants through agc_add_gpu_heap_test(), so this PR's GPU heap test list needs no edit.

The other conflicts keep both sides:
- ShaderDiskCache.cpp: #9508's 866bd70c70 drops the second encode and decode of userDataRegisterReads, and this PR's device image interface and sampler class lists follow imageTableFaults in both.
- Dispatch.cpp and DrawCapture.cpp: this PR's DeviceImagesServe line, then #9508's 675e23803d heap key and profile lines.
- ShaderResources.cpp: this PR's DeviceImageTable.hpp include and #10194's MemoryAccounting.hpp include.
```
Push to fork `prm/7642-9508-37c94cd099`.

## Checks and runs

All of PRM-104c's "Checks", "Builds and runs", "Identity" and comment-line checks, with these specifics:
- **Nothing else changed:** the diff against `git merge-tree --write-tree b5065a1fe9 37c94cd099`'s tree may list only:
  - `libSceAgcDriver/CMakeLists.txt`;
  - `libSceAgcDriver/tests/cmake/*.cmake`;
  - the four C++ files, in them only the five hunks.
- **The count:** reuse PRM-104c's (a) 3,219 and (b) 3,193. (c) must again be 3,227 = (a) ∪ (b), with all 22 `_gpu_heap` variants.
- **The runs:**
  - #7642's set ×10, as in PRM-104b;
  - every test from the files 31f5d0e8dd..37c94cd099 changes ×10, mapped as PRM-104b did, with `agc_shader_disk_cache` and `agc_shader_memory` included by name;
  - `^(agc|shader)` once.
- **The disk-cache round trip:** `agc_shader_disk_cache` must pass. If it has no case that writes and reads back an invocation with non-empty `deviceImageInterfaces` and `deviceSamplerClasses`, say so; don't add one.

## Result

`results/cloud-<n>/PRM-104d.md`:
- the heads used and the merge SHA;
- the five hunks as committed;
- the count;
- every run's summary line, with skip reasons;
- the checks and the identity.

No PR branch is touched, and nothing is posted.

## When you finish

After you mark this task done, claim the next open task on the board right away (priority high first, then the lowest id; skip any whose `for:` line names another worker). If none is open, restart the board watcher as a background command before you end your turn. A worker that ends its turn with no task and no watcher is never woken: on 2026-10-15 one sat READY for 30 minutes beside two open tasks.
