# PRM-104e: #7642 + #9508's 09cc865e29: stopped, `resolve` leaves a fragment conflict. A dry run finds three more edits needed (cloud-2)

**Stopped before committing.** `python3 tools/agc_test_split.py resolve` exits 1 and leaves a conflict marker in `tests/cmake/agc_shader_memory_tests.cmake`. PRM-104c's rule, which PRM-104e keeps, makes that a stop.
- **Nothing committed or pushed:** no merge commit, no `prm/7642-9508-*` branch, no PR branch touched, nothing posted.
- **Dry run:** to save a round, I applied PRM-104e's seven named resolutions and a proposed fix for the fragment to the uncommitted merge, then built, configured and ran it.
  - The tree compiles only with **three more edits outside the named resolutions** (§3).
  - With all four extra edits, the dry-run tree builds (exit 0), the count rule holds, and one pass of #7642's set and of `^(agc|shader)` passes (§4).
  - I then aborted the merge. My worktree is back at b5065a1fe9, clean.

**What the PR Manager needs to decide:** a PRM-104f with these four edits named, beyond PRM-104e's seven resolutions:

| # | file | edit | why |
|---|---|---|---|
| E1 | `tests/cmake/agc_shader_memory_tests.cmake` | the `if(WIN32)` `set_tests_properties(… ENVIRONMENT_MODIFICATION …)` line becomes #9508's list plus #7642's two names (§2) | `resolve` can't merge that line. #9508's 09cc865e29 dropped four names from it, and #7642 adds two. |
| E2 | `SpirvBackend/src/SpirvImageEmitter.cpp` (#7642's 85b3d403d9 lines) | `state.tableHeap` → `state.TableHeap()`, `state.tableMapAddressed` → `state.TableMapAddressed()`, 3 uses on 2 lines (§3.1) | #9508's 0f4b636d04 replaced the two members with methods. Clean merge, compile error. |
| E3 | `Execution/src/Driver/Draw/DrawCapture.cpp`, PRM-104c §2.3's resolved line | `localDevice->DeviceImagesServe(…)` → `context.localDevice->DeviceImagesServe(…)` (§3.2) | #9508's 675e23803d made `materializeDrawStage` take a `DrawStageContext`, so the bare `localDevice` is gone. PRM-104c §2.3's "Proposed" text doesn't compile at this head. |
| E4 | #7642's `tests/execution/DeviceImagePath.cpp` (1 use) and `tests/execution/DeviceImageTable.cpp` (19 uses) | `device->GraphicsContext()` → `device->graphicsContext()` (§3.3) | #9508's 675e23803d renamed `VulkanDevice::GraphicsContext()`. Clean merge, compile error. |

`Dispatch.cpp`'s PRM-104c §2.2 line (`localDevice->DeviceImagesServe(address)`) compiles as proposed; `localDevice` is still in scope there.

## 1. Start: the head rule passes

| ref | at 02:25:25Z | |
|---|---|---|
| `upstream refs/pull/9508/head` | **09cc865e29c365a5b8152d68f2fb8f8d3f963872** | = 09cc865e29, so `merge-base --is-ancestor` is trivially true |
| `upstream refs/pull/7642/head` | 06eba99c5080f540d05b5c6227958206e54d7cdc | = 06eba99c50 |
| base | b5065a1fe9 (`fork prm/7642-9508-31f5d0e8dd`) | as given |

**`git merge-tree --write-tree b5065a1fe9 09cc865e29`** (tree 1fad3317bf) conflicts in exactly PRM-104d's files:
- `libSceAgcDriver/CMakeLists.txt`: 3 hunks.
- PRM-104c's five C++ hunks, byte for byte:
  - `ShaderDiskCache.cpp` ×2;
  - `Dispatch.cpp`;
  - `DrawCapture.cpp`;
  - `ShaderResources.cpp`.
- `tests/BindlessHeapSlots.cpp`: 1 hunk, PRM-104d §2's text.
- Nothing else.

No submodule pin changes between b5065a1fe9 and 09cc865e29.

**The merge,** in my worktree on b5065a1fe9 (detached): `git -c rerere.enabled=false merge --no-ff --no-commit 09cc865e29…`. It reported the same six conflicted files.

## 2. The stop: `resolve` exits 1

**What `resolve` printed:** `CMakeLists.txt` and the four fragments of PRM-104c §3, then `resolve the conflict markers in core/libs/prx/libSceAgcDriver/tests/cmake/agc_shader_memory_tests.cmake, then git add them`. Exit code 1.

**The conflict it leaves** (line 50 of the fragment, inside `if(WIN32)`):
```
<<<<<<< …/agc_shader_memory_tests.cmake (split)
        set_tests_properties(agc_shader_table_snapshot_reuse agc_shader_table_snapshot_reuse_unmasked agc_driver_image_table_vouched_lookups agc_driver_image_table_vouch_queued_draws PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
=======
        set_tests_properties(agc_shader_table_snapshot_reuse agc_shader_table_snapshot_reuse_unmasked agc_shader_table_trailer agc_shader_device_image_tables agc_driver_image_table_vouched_lookups agc_driver_image_table_vouch_queued_draws agc_driver_image_table_cache_bounds_heap_small agc_driver_image_table_atomics agc_driver_image_table_atomics_gpu_heap agc_driver_image_table_store_gpu_heap PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
>>>>>>> …/agc_shader_memory_tests.cmake (change)
```

**The cause:**
- **#9508's 09cc865e29** "test(agc): tidy the GPU heap tests" is the only commit in 37c94cd099..09cc865e29 that touches this fragment.
  - It re-registers `agc_driver_image_table_atomics`, `_atomics_gpu_heap`, `_store_gpu_heap` and `agc_driver_image_table_cache_bounds_heap_small` through a new `agc_add_env_test()` (`CMakeLists.txt:628–641` at 09cc865e29).
  - That helper sets the `if(WIN32)` `ENVIRONMENT_MODIFICATION` itself (:638–640), so the commit drops those four names from the shared `if(WIN32)` line.
- **#7642's version of that line** (pre-split, `CMakeLists.txt:2968` at b5065a1fe9) inserts `agc_shader_table_trailer agc_shader_device_image_tables` after `…_unmasked`.
- At PRM-104c's 37c94cd099 the fragment still had the long line, so `resolve` merged it cleanly there.

**Proposed (E1):** #9508's line with #7642's two names, in #7642's place:
```
        set_tests_properties(agc_shader_table_snapshot_reuse agc_shader_table_snapshot_reuse_unmasked agc_shader_table_trailer agc_shader_device_image_tables agc_driver_image_table_vouched_lookups agc_driver_image_table_vouch_queued_draws PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
```
- It's a Windows-only line, so the Linux configure is the same either way.
- The four tests #9508 moved keep their `PATH` through `agc_add_env_test()`.

## 3. The dry run: three more edits the tree needs

**Applied, uncommitted:** PRM-104e's seven resolutions exactly as listed, plus E1, on the stopped merge.
- **ShaderDiskCache.cpp, encode:** `writer.Value(invocation.imageTableFaults);` followed by #7642's two `writer.List` lines.
- **ShaderDiskCache.cpp, decode:** the same with `reader`. After the edit, `userDataRegisterReads` appears twice, once per direction.
- **Dispatch.cpp, DrawCapture.cpp, ShaderResources.cpp:** as PRM-104c §2.
- **BindlessHeapSlots.cpp:** as PRM-104d §2, with both `SetTestEnvironment` calls before `OpenVulkanTestDevice()`.
- **SpirvDescriptors.cpp:** the two `tableMapWord(` → `LoadTableMap(` calls. Afterwards `git grep -n tableMapWord` finds nothing.

**Builds:** CI's Linux flags plus `-DANYPS5_ENABLE_SPIRV_TOOLS=ON`, incremental from b5065a1fe9, `-k 0`. Each break below was found by one build and fixed before the next.

### 3.1 E2: `SpirvImageEmitter.cpp` (build 1, 1 failed step)

```
SpirvImageEmitter.cpp:1990:16: error: 'struct ShaderRecompiler::SpirvEmitterState' has no member named 'tableHeap'; did you mean 'TableHeap'?
SpirvImageEmitter.cpp:1990:36: error: … no member named 'tableMapAddressed'; did you mean 'TableMapAddressed'?
SpirvImageEmitter.cpp:2036:57: error: … no member named 'tableMapAddressed'; did you mean 'TableMapAddressed'?
```

- **The lines** are #7642's 85b3d403d9, in `EmitDeviceTableAccess` and `DefineDeviceImageLookup`. They are lines 1986 and 2032 at b5065a1fe9.
- **#9508's 0f4b636d04** "share the GPU heap emitter helpers" removed the members `bool tableMapAddressed` and `bool tableHeap`.
  - In their place come `SpirvEmitterState::TableMapAddressed()` (`tableMapVariable != 0 && UsesDataAddress()`) and `TableHeap()` (`gpuResolvedImages && UsesDataAddress()`), at `SpirvEmitter.cpp:86–93`.
  - It switched #9508's own callers to the methods.
- **The fix:**
```
-    if (!state.tableHeap || !state.tableMapAddressed) ctx.Fail(inst, "reads a device-table image outside a GPU-mode stage that reads its table map by address");
+    if (!state.TableHeap() || !state.TableMapAddressed()) ctx.Fail(inst, "reads a device-table image outside a GPU-mode stage that reads its table map by address");
-    if (state.deviceImageLookupFunction != 0u || !state.tableMapAddressed || std::none_of(…)) return;
+    if (state.deviceImageLookupFunction != 0u || !state.TableMapAddressed() || std::none_of(…)) return;
```

### 3.2 E3: `DrawCapture.cpp` (build 2, 1 failed step)

```
DrawCapture.cpp:167:78: error: 'localDevice' was not declared in this scope
```

- **The line** is PRM-104c §2.3's proposed `if (request.target.deviceImageTables) request.target.deviceImageTables = localDevice->DeviceImagesServe(program.binary.codeAddress);`.
- **#9508's 675e23803d** changed `materializeDrawStage(std::size_t i, …, localDevice, …)` to `materializeDrawStage(const DrawStageContext& context, …)`. The function's other use of the device is now `context.localDevice->Target()` (`DrawCapture.cpp:160` at 09cc865e29).
- **The fix:**
```
-    if (request.target.deviceImageTables) request.target.deviceImageTables = localDevice->DeviceImagesServe(program.binary.codeAddress);
+    if (request.target.deviceImageTables) request.target.deviceImageTables = context.localDevice->DeviceImagesServe(program.binary.codeAddress);
```
- The next line stays #9508's: `const auto waitedBefore = traceCapSync() || context.profile ? …`.

### 3.3 E4: `GraphicsContext()` in #7642's two test files (build 3, 2 failed steps)

```
tests/execution/DeviceImagePath.cpp:347:57: error: 'class AgcDriver::VulkanDevice' has no member named 'GraphicsContext'; did you mean 'graphicsContext'?
tests/execution/DeviceImageTable.cpp:193:33: error: … (and 18 more lines: 208, 247, 266, 302, 323, 407, 464, 490, 521, 550, 566, 596, 620, 648, 676, 697, 763, 768)
```

- **#9508's 675e23803d** renamed `VulkanDevice::GraphicsContext()` to `graphicsContext()` (`VulkanDevice.hpp:379` at 09cc865e29).
- #7642's two test files call the old name 20 times, including `HeapRetention`, which PRM-104b edited.
- **The fix:** `device->GraphicsContext()` → `device->graphicsContext()`, 1 use in `DeviceImagePath.cpp` and 19 in `DeviceImageTable.cpp`. Nothing else on those lines changes.
- PRM-104c's merge never built, which is why this didn't show before.

**Build 4, with E1–E4:** exit 0. Every step built and linked.

## 4. The dry-run tree's numbers (uncommitted, for information)

**What differs from git's own merge:** `git diff 1fad3317bf` of the dry-run tree touches 14 files.
- `CMakeLists.txt` and the four fragments;
- the four PRM-104c C++ files;
- `BindlessHeapSlots.cpp`;
- `SpirvDescriptors.cpp` (the 2 calls);
- E2's `SpirvImageEmitter.cpp` (2 lines);
- E4's `DeviceImagePath.cpp` (1 line) and `DeviceImageTable.cpp` (19 lines).

E1 is inside the `agc_shader_memory_tests.cmake` fragment, and E3 is inside the `DrawCapture.cpp` hunk.

**The count:** CI's `CONFIGURE` plus `-DANYPS5_ENABLE_SPIRV_TOOLS=ON`, each tree at its own submodule pins.

| tree | tests | duplicates | 22 `_gpu_heap` |
|---|---:|---:|---:|
| (a) #9508 09cc865e29 | **3,219** | 0 | 22 |
| (b) b5065a1fe9 | **3,193** (measured; PRM-104c had the same) | 0 | 22 |
| (c) the dry-run merge | **3,227** | 0 | 22 |

- **(c) = (a) ∪ (b)** exactly: nothing from (a) or (b) is missing, and nothing new beyond them.
- (b) adds 8 names over (a), the same 8 as PRM-104c §6: `agc_device_image_keys`, `agc_driver_device_image_path`, `_gpu_path`, `_without_heap`, `agc_driver_device_image_table`, `_heap`, `agc_shader_device_image_tables`, `agc_shader_table_trailer`.

**One pass each** (lavapipe, as `runner`, `ANYPS5_REQUIRE_VULKAN=1 GALLIUM_OVERRIDE_CPU_CAPS=avx`; not the ×10 runs):
- **#7642's set plus `agc_shader_disk_cache`, `agc_shader_memory`, `agc_shader_table_trailer`:** "100% tests passed, 0 tests failed out of 60", 15 skipped.
  - The skips include `agc_driver_device_image_table_heap` and all 9 `agc_driver_heap_slots*` tests (the `BindlessHeapSlots.cpp` tests): no `VK_EXT_descriptor_heap`.
  - `agc_shader_disk_cache`, `agc_shader_memory`, `agc_shader_table_trailer`, `agc_shader_device_image_tables` and `agc_driver_device_image_table` passed.
- **`^(agc|shader)` once:** "100% tests passed, 0 tests failed out of 1292", 171 skipped.
  - That is more skips than b5065a1fe9's 104, because 09cc865e29's GPU heap variants "now skip instead of passing on the per-draw path when the device has no descriptor heap".

**Not done, because of the stop:**
- the commit and the push;
- the ×10 runs and the 31f5d0e8dd..09cc865e29 test mapping;
- `check_test_registration.py` and `check_conventions.py`;
- the own-diff comparison;
- the identity and comment-line checks;
- the `LoadTableMap` heap-branch and disk-cache round-trip answers.

With E1–E4 named, a redo is: the same merge, the seven resolutions, E1–E4, then the task's checks and runs on the committed merge.

## State

- **Worktree:** `p095` is back at b5065a1fe9, clean (`git merge --abort`).
- **The dry-run resolutions are saved on this worker** as a patch against git's merge tree 1fad3317bf, for reuse: 14 files, +56 −1571, nearly all of it the `CMakeLists.txt` registrations moving to the fragments.
- **Constraints kept:** no commit and no push of any code branch, nothing upstream or on a PR. The only pushes are this result, STATUS and the board.
