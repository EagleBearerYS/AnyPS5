# PRM-104f: #7642: PRM-104e's merge of #9508's 09cc865e29 into b5065a1fe9, with its seven resolutions and E1-E4

from: AnyPS5 PR Manager
for: any
priority: high

## Context

- **PRM-104e** (`results/cloud-2/PRM-104e.md`) stopped before committing.
  - **E1:** `resolve` left one conflicted line in `tests/cmake/agc_shader_memory_tests.cmake`.
  - **E2-E4:** its dry run found three compile fixes, adaptations to names #9508 changed.
  - With all four, the dry-run tree built and passed.
- **The decision (the user's OK, 11-01):** do PRM-104e exactly, with E1-E4 added as its §1-§3 propose, each named in the merge message.
- Everything else in PRM-104e stands: the head rule, the seven resolutions, the checks, the runs and the result format.
- **If you are cloud-2,** reuse your saved dry-run patch against git's merge tree 1fad3317bf, after checking it matches the edits below.

## Start

PRM-104e's head rule, unchanged: use #9508's head at your start if it descends from 09cc865e29 and the dry run gives exactly PRM-104e's conflicts, now including the `agc_shader_memory_tests.cmake` line that `resolve` leaves. #7642 must be 06eba99c50. Anything else is a stop.

## The edits (PRM-104e's seven, plus these four, and nothing else)

- **E1, `tests/cmake/agc_shader_memory_tests.cmake`, the `if(WIN32)` line `resolve` leaves:** #9508's line with #7642's two names after `agc_shader_table_snapshot_reuse_unmasked`, exactly as PRM-104e §2's "Proposed" line. Then `git add` the fragment and check that no unmerged file and no conflict marker is left.
- **E2, `SpirvBackend/src/SpirvImageEmitter.cpp`:** in #7642's two lines (`EmitDeviceTableAccess` and `DefineDeviceImageLookup`), `state.tableHeap` becomes `state.TableHeap()` and `state.tableMapAddressed` becomes `state.TableMapAddressed()`. That's 3 uses, as PRM-104e §3.1 shows.
- **E3, `Execution/src/Driver/Draw/DrawCapture.cpp`:** the resolved `DeviceImagesServe` line uses `context.localDevice->DeviceImagesServe(program.binary.codeAddress)`, as PRM-104e §3.2 shows.
- **E4, #7642's tests:** `device->GraphicsContext()` becomes `device->graphicsContext()`, 1 use in `tests/execution/DeviceImagePath.cpp` and 19 in `tests/execution/DeviceImageTable.cpp`. Nothing else on those lines changes.

Afterwards, `git grep -n -E 'tableMapWord|\.tableHeap\b|\.tableMapAddressed\b|GraphicsContext\(\)'` on the merged tree must find nothing.

## Commit

PRM-104e's message, with these lines added to its bullet list under "The other conflicts keep both sides":
```
- tests/cmake/agc_shader_memory_tests.cmake: #9508's 09cc865e29 moves four tests to agc_add_env_test() and drops them from the Windows ENVIRONMENT_MODIFICATION line, and this PR's agc_shader_table_trailer and agc_shader_device_image_tables stay on it.
```
Then replace its last paragraph with:
```
#9508's 0f4b636d04 and 675e23803d rename what this PR's lines call, so they follow: TableTrailerWord calls the shared LoadTableMap in place of tableMapWord, SpirvImageEmitter.cpp's device table checks call TableHeap() and TableMapAddressed(), the DrawCapture.cpp line reads context.localDevice, and the device image tests call graphicsContext().
```
Push to fork `prm/7642-9508-<short sha>`.

## Checks and runs

PRM-104e's, all of them, on the committed merge. **A failure in any ×10 run or in `^(agc|shader)` is a stop:** the dry run had 0, so report the test and its output, and don't push. In particular:
- **Nothing else changed:** the files PRM-104e §4 lists, and in them only the edits above.
- the count (c) = (a) ∪ (b) = 3,227 at 09cc865e29, with all 22 `_gpu_heap` variants;
- the ×10 runs, including the `BindlessHeapSlots.cpp` tests (`agc_driver_heap_slots*`), `agc_shader_disk_cache`, `agc_shader_memory` and `agc_shader_table_trailer` by name;
- `^(agc|shader)` once;
- `check_test_registration.py` and `check_conventions.py --base <#9508 head>`;
- the own-diff comparison against `git diff 31f5d0e8dd b5065a1fe9`;
- the identity and 0 added comment lines;
- the `LoadTableMap` heap-branch answer: which tests reach it, or that every one skips, with the skip line;
- the disk-cache round-trip answer.

## Result

`results/cloud-<n>/PRM-104f.md`, in PRM-104e's format: the heads and merge SHA, each edit as committed, the count, the run lines with skip reasons, the two answers, the checks and the identity.

No PR branch is touched, and nothing is posted.

## When you finish

After you mark this task done, claim the next open task on the board right away (priority high first, then the lowest id; skip any whose `for:` line names another worker). If none is open, restart the board watcher as a background command before you end your turn. A worker that ends its turn with no task and no watcher is never woken: on 2026-10-15 one sat READY for 30 minutes beside two open tasks.
