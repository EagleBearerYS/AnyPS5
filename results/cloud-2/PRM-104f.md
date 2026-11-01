# PRM-104f: #7642 merged with #9508's 09cc865e29, with PRM-104e's seven resolutions and E1-E4 (cloud-2)

**Merged, checked and pushed.** Every run passed.

| Item | Value |
|---|---|
| Heads | #9508 `refs/pull/9508/head` = **09cc865e29** (`09cc865e29c365a5b8152d68f2fb8f8d3f963872`) at 02:56:50Z, so the head rule holds trivially. #7642 = 06eba99c50. Base b5065a1fe9. Neither moved while I worked. |
| Merge | **715097ee91** (`715097ee9171ecea330470298057b0b2d9dbbdc6`), parents b5065a1fe9 and 09cc865e29, on fork `prm/7642-9508-09cc865e29` (pushed; `git ls-remote fork` reads 715097ee91). No PR branch touched, nothing posted. |
| Conflicts | Exactly PRM-104e's: `CMakeLists.txt` (3 hunks), PRM-104c's five C++ hunks, `tests/BindlessHeapSlots.cpp`, and the one `agc_shader_memory_tests.cmake` line that `resolve` leaves |
| Edits | PRM-104e's seven, plus E1-E4, and nothing else. The committed tree is byte-identical to PRM-104e's saved dry run (§1). |
| Build | Full build with CI's Linux flags and `-DANYPS5_ENABLE_SPIRV_TOOLS=ON`: **exit 0**, 3853/3853 steps |
| Count | (a) 3,219, (b) 3,193, **(c) 3,227 = (a) ∪ (b)**. No duplicates, all 22 `_gpu_heap` variants. |
| #7642's set ×10 | **"100% tests passed, 0 tests failed out of 60"**: 45 tests passed 10/10 (450 runs), 15 skipped |
| 31f5d0e8dd..09cc865e29 tests ×10 | **"100% tests passed, 0 tests failed out of 366"**: 328 tests passed 10/10 (3,280 runs), 38 skipped |
| `^(agc\|shader)` once | **"100% tests passed, 0 tests failed out of 1292"**, 171 skipped |
| Checks | `check_test_registration.py build`: "1670 test executable(s), 0 built and not run as recorded, none new"; `check_conventions.py --base 09cc865e29`: "All conventions checks passed." |
| `LoadTableMap` | The trailer read can only take `LoadTableMap`'s `TableMapAddressed()` branch, never the table-map buffer branch. It is emitted and validated at compile level, and executed only by tests that skip here (§4). |
| Disk cache | `agc_shader_disk_cache` passed 10/10. Its default run round-trips non-empty `deviceImageInterfaces` and `deviceSamplerClasses` (§5). |
| Identity | author and committer `Yunus Shaikh <75296019+EagleBearerYS@users.noreply.github.com>`, unsigned (`%G?` = N), no trailers |
| Added comment lines | `git diff 09cc865e29 715097ee91 \| grep -cE '^\+\s*//'`: **0** |

## 1. The edits as committed

**The merge:** in my worktree on b5065a1fe9 (detached), `git -c rerere.enabled=false merge --no-ff --no-commit 09cc865e29…`.
- It conflicted in exactly the six expected files.
- `python3 tools/agc_test_split.py resolve` (the merged tree's copy) wrote `CMakeLists.txt` and four fragments. It left the one `agc_shader_memory_tests.cmake` line, as PRM-104e §2 found, and exited 1.
- I then made the edits below and ran `git add`.
- `git ls-files -u` was empty, and no changed file has a conflict marker.

**PRM-104e's seven:**
1. **`ShaderDiskCache.cpp`, encode and decode:** after `writer.Value(invocation.imageTableFaults);` / `reader.Value(invocation.imageTableFaults);`, #7642's two `List` lines. #9508's 866bd70c70 removal of the duplicate `userDataRegisterReads` is kept, so the file names it once per direction.
2. **`Dispatch.cpp`:** #7642's `if (request.target.deviceImageTables) request.target.deviceImageTables = localDevice->DeviceImagesServe(address);`, then #9508's `HeapFragmentKey(codeOffset, HeapKeyClass::Compute)` and the `HeapFragmentKnown(&snapshot, computeHeapKey)` line.
3. **`DrawCapture.cpp`:** #7642's `DeviceImagesServe` line (with E3, below), then #9508's `context.profile` line.
4. **`ShaderResources.cpp`:** `#include …/DeviceImageTable.hpp` followed by `#include …/MemoryAccounting.hpp`.
5. **`tests/BindlessHeapSlots.cpp`:** `SetTestEnvironment("APS5_GPU_HEAP", "1");` and `SetTestEnvironment("APS5_HEAP_SLOT_CAP", comesBack ? "8" : "1");`, then `auto device = OpenVulkanTestDevice();`, the null check and the `graphicsContext().descriptorHeap` check.
6. **`SpirvDescriptors.cpp`, `TableTrailerWord`:** both `tableMapWord(` calls become `LoadTableMap(`, with the same arguments.
7. **CMake:** `resolve` alone, with no 22-name edit. `FormatVersion` stays 62.

**E1, `tests/cmake/agc_shader_memory_tests.cmake`:**
```
        set_tests_properties(agc_shader_table_snapshot_reuse agc_shader_table_snapshot_reuse_unmasked agc_shader_table_trailer agc_shader_device_image_tables agc_driver_image_table_vouched_lookups agc_driver_image_table_vouch_queued_draws PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
```

**E2, `SpirvBackend/src/SpirvImageEmitter.cpp`, 3 uses on 2 lines:**
- `!state.TableHeap() || !state.TableMapAddressed()` in `EmitDeviceTableAccess`;
- `!state.TableMapAddressed()` in `DefineDeviceImageLookup`.

**E3, `DrawCapture.cpp:167`:** `… = context.localDevice->DeviceImagesServe(program.binary.codeAddress);`.

**E4, `GraphicsContext()` → `graphicsContext()`:** 1 use in `tests/execution/DeviceImagePath.cpp` and 19 in `tests/execution/DeviceImageTable.cpp`.
- **One note on the form:** 2 of the 19 are written `device->GraphicsContext()`, and 17 are `device.GraphicsContext()` (on a `VulkanDevice&`).
- They are the 19 lines PRM-104e §3.3 listed, at 193, 208 … 763 and 768. The rename is the same token on each, and nothing else on those lines changes.
- That is the only way `git grep -n -E 'tableMapWord|\.tableHeap\b|\.tableMapAddressed\b|GraphicsContext\(\)'` can find nothing, and it does find nothing on the merged tree.

**Dry-run match:** `git diff 1fad3317bf` of the committed tree (14 files) is byte-identical to the patch saved in PRM-104e (`cmp` equal).

**The commit message** is PRM-104e's text, with:
- the `tests/cmake/agc_shader_memory_tests.cmake` bullet added under "The other conflicts keep both sides";
- the last paragraph replaced by the task's "#9508's 0f4b636d04 and 675e23803d rename what this PR's lines call, …".

## 2. Nothing else changed, and the own diff

**Against git's own merge tree** (`git merge-tree --write-tree b5065a1fe9 09cc865e29` = 1fad3317bf), the diff lists only these files, and in each only the edits above:
- `libSceAgcDriver/CMakeLists.txt` and the four fragments: `agc_device_image_keys_tests`, `agc_driver_device_image_path_tests`, `agc_driver_device_image_table_tests`, `agc_shader_memory_tests`;
- `ShaderDiskCache.cpp`, `Dispatch.cpp`, `DrawCapture.cpp`, `ShaderResources.cpp`;
- `tests/BindlessHeapSlots.cpp`;
- `SpirvDescriptors.cpp` (2 calls), `SpirvImageEmitter.cpp` (2 lines);
- `DeviceImagePath.cpp` (1 line), `DeviceImageTable.cpp` (19 lines).

**Own diff:** the sorted `+`/`−` lines of `git diff 09cc865e29 715097ee91` (60 files, +4149 −32) against `git diff 31f5d0e8dd b5065a1fe9` (56 files, +4144 −37). 36 lines on each side differ, all expected:
- **The registration move:**
  - #7642's `CMakeLists.txt` `foreach(agcTest IN ITEMS …)` change is now `agc_add_default_test(agc_device_image_keys_tests)` in its fragment.
  - The three new fragments add `if(BUILD_TESTING)` / `endif()` wrappers.
  - The four fragment files are the only files in the new diff that aren't in the old.
- **E1:** the `if(WIN32)` line's `−`/`+` pair is now against #9508's 6-name line.
- **E2:** 2 lines; **E3:** 1 line; **E4:** 20 lines.
- **PRM-104d §3:** `TableTrailerWord`'s 2 `LoadTableMap` lines.
- **Lines #9508 changed underneath, in `BindlessHeapSlots.cpp`:**
  - The old diff moved the `_putenv_s`/`setenv` pair inside `#ifdef _WIN32 … #endif`.
  - The new diff moves the `SetTestEnvironment("APS5_HEAP_SLOT_CAP", …)` line above the device open.
  - Same change, on #9508's helper.

## 3. Runs

**Setup:**
- Worktree `p095`, incremental from b5065a1fe9's build.
- All 24 submodules at the merge's pins (none changed). Vulkan-Headers is 5d94bb4dcc.
- CI's Linux flags plus `-DANYPS5_ENABLE_SPIRV_TOOLS=ON`.
- lavapipe (llvmpipe, LLVM 20.1.2), as `runner`, with `ANYPS5_REQUIRE_VULKAN=1 GALLIUM_OVERRIDE_CPU_CAPS=avx`.

**Build:** exit 0, 3853/3853 steps, 330 s.

**#7642's set ×10** (PRM-095 §6.3's 38 names plus `^agc_driver_heap_slots|^agc_driver_image_table|^agc_driver_device_image`, `--repeat until-fail:10 --timeout 600`):
- **"100% tests passed, 0 tests failed out of 60"**, 307.61 s. 45 tests 10/10 (450 runs), 0 failed runs.
- `agc_shader_disk_cache`, `agc_shader_memory`, `agc_shader_table_trailer`, `agc_shader_device_image_tables` and `agc_driver_device_image_table`: 10/10 each.
- 15 skipped:
  - 9 `agc_driver_heap_slots*` (all of `tests/BindlessHeapSlots.cpp`'s tests): "skipped, the device has no VK_EXT_descriptor_heap";
  - 4: "skipped, the device lacks VK_EXT_descriptor_heap, or the maintenance5, slot room and push data the GPU heap needs with it, so the GPU heap has nothing to run through". `agc_driver_image_table_store_gpu_heap` is one of them; under 09cc865e29's helper it skips with this message;
  - `agc_driver_device_image_table_heap`: "skipped, the device lacks VK_EXT_descriptor_heap, which the GPU heap the device image table takes its slots from needs";
  - `agc_driver_device_image_path_gpu_path`: "skipped, the device lacks VK_EXT_descriptor_heap, which the GPU heap the device image path reads its tables through needs".

**Every test from the files 31f5d0e8dd..09cc865e29 changes, ×10:**
- **Mapping,** as PRM-104b did: the range's 108 changed test sources go to their executables through `build.ninja`, and from there to their registrations through `CTestTestfile.cmake`. That gives 107 executables and **366 tests**: 269 `agc_*` plus 97 others (guest, audio, ngs2, …), because the range carries #9508's main merges.
- **Not mapped:** four changed shared headers that no test executable compiles directly: `tests/execution/HeapSampleKernel.hpp`, `TargetAddressWindow.hpp`, `VulkanTestDevice.hpp` and `core/libs/tests/FontFixture.hpp`. Their AGC users are all in the `^(agc|shader)` sweep.
- **The set includes by name:** `agc_shader_disk_cache`, `agc_shader_memory`, `agc_shader_table_trailer` and the 9 `agc_driver_heap_slots*` tests.
- **Result:** **"100% tests passed, 0 tests failed out of 366"**, 444.87 s (`-j4`). 328 tests 10/10 (3,280 runs), 0 failed runs.
- **38 skipped:**
  - 14: GPU heap, "lacks VK_EXT_descriptor_heap, or the maintenance5, slot room and push data …";
  - 12: "the device has no VK_EXT_descriptor_heap", the `agc_driver_heap_slots*` tests among them;
  - 7: host imports or watched host memory (unit shadows, clear-key hand-over, the test block not imported, no deferred settle on Linux, no host memory type for the image);
  - 1 each: wave64 on narrower subgroups, no pipeline cache, native FMASK, no usable IPv6 loopback (a guest network test).

**`^(agc|shader)` once, `-j4 --timeout 600 --output-on-failure`:**
- **"100% tests passed, 0 tests failed out of 1292"**, 131.30 s. No known flake failed, and there were no disk failures.
- 171 skipped; I reran each once with `-V`:

| Count | Reason |
|---|---|
| 95 | no `VK_EXT_descriptor_heap`, or the GPU heap that needs it. 09cc865e29's GPU heap variants "now skip instead of passing on the per-draw path", which is why this is much higher than b5065a1fe9's 104 total skips. |
| 39 | wave64 programs on lavapipe's narrower subgroups |
| 12 | host imports or write watching (`/dev/udmabuf`) |
| 9 | multisample colour attachments, native FMASK |
| 7 | no mesh shader compiler (llvmpipe) |
| 9 | one each: conservative rasterization, native f16, packed f16, `VK_EXT_sample_locations` at 4 samples, tessellation subgroup operations, pipeline cache, sampleable 2x depth, 2x colour and depth targets, executable statistics |

## 4. The `LoadTableMap` branch

**The code** (`SpirvImageEmitter.cpp` at the merge):
- `LoadTableMap(state, index)` has two branches:
  - `if (state.TableMapAddressed())`: an `OpLoad` through `EmitStageRingWordPointer(state, RootStageField::TableMap, index)`, the pushed ring address;
  - otherwise an `OpAccessChain` into `state.tableMapVariable`, the table-map storage buffer.
- #7642's trailer read is `TableTrailerWord` (`SpirvDescriptors.cpp:107`).
  - Its only caller is `TrailerAddress` (`SpirvImageEmitter.cpp:1953`).
  - `TrailerAddress`'s only callers are the two trailer loads in `DefineDeviceImageLookup` (:2080–2081).
- `DefineDeviceImageLookup` returns at once unless `state.TableMapAddressed()` (:2036, E2's line).
- Likewise `EmitDeviceTableAccess` fails unless `state.TableHeap() && state.TableMapAddressed()` (:1990).

**So the trailer is read only through the `TableMapAddressed()` branch.** The table-map buffer branch is never reached by a trailer read, on any device. That is a property of the code, not of this machine.

**Which tests reach a shader that reads the trailer:**
- **`agc_shader_device_image_tables` (compile level, no GPU execution):**
  - It compiles a GPU-mode stage with a device table.
  - It checks that "the lookup … read[s] the bucket and ring addresses from the four trailer words before the element base".
  - It validates the SPIR-V (`ValidateAndOptimizeSpirv`).
  - It **passed 10/10** in both ×10 runs and once in the sweep.
- **On a GPU,** only the device-image heap tests execute such a shader. Both skip here:
  - `agc_driver_device_image_table_heap`: "skipped, the device lacks VK_EXT_descriptor_heap, which the GPU heap the device image table takes its slots from needs";
  - `agc_driver_device_image_path_gpu_path`: "skipped, the device lacks VK_EXT_descriptor_heap, which the GPU heap the device image path reads its tables through needs".
- **`agc_shader_table_trailer`** (passed 10/10) checks the trailer's host-side layout (`AppendTrailer`, `ReadTrailer`). It doesn't compile a lookup that reads it.
- **Plainly:** the shader-side trailer read is compiled and validated on this machine but not executed on a GPU, because lavapipe has no `VK_EXT_descriptor_heap`.

## 5. The disk-cache round trip

- `agc_shader_disk_cache` (`agc_add_default_test(agc_shader_disk_cache_tests)`, no arguments) **passed 10/10**.
- Its default run calls `verifyResultRoundTrip()` (`tests/ShaderDiskCache.cpp:2657`):
  - `sampleResult()` (:256) sets `deviceImageInterfaces = {0x1234567890abcdef, 0, 42}` and `deviceSamplerClasses = {3, 0}` (:271–272);
  - `ShaderDiskCache::EncodeResult` writes it through `encodeInvocation` (`ShaderDiskCache.cpp:1989–1992`), the resolved hunk;
  - `DecodeResult` reads it back through `decodeInvocation` (:1997–2000);
  - `requireSameResult` compares both lists (:200).
- **So a case does write and read back an invocation with non-empty `deviceImageInterfaces` and `deviceSamplerClasses`,** on the merged layout (no second `userDataRegisterReads`). It passes.

## 6. Checks and identity

- `python3 tools/check_test_registration.py build`: "1670 test executable(s), 0 built and not run as recorded, none new".
- `python3 tools/check_conventions.py --base 09cc865e29`: "All conventions checks passed."
- **Count,** CI's `CONFIGURE` plus `-DANYPS5_ENABLE_SPIRV_TOOLS=ON`, each tree at its own pins:

| tree | tests | duplicates | 22 `_gpu_heap` |
|---|---:|---:|---:|
| (a) 09cc865e29 | 3,219 | 0 | 22 |
| (b) b5065a1fe9 | 3,193 | 0 | 22 |
| (c) 715097ee91 | **3,227** | 0 | 22 |

  - (c) = (a) ∪ (b) exactly.
  - The 8 names (b) adds are #7642's: `agc_device_image_keys`, `agc_driver_device_image_path`, `_gpu_path`, `_without_heap`, `agc_driver_device_image_table`, `_heap`, `agc_shader_device_image_tables`, `agc_shader_table_trailer`.
- **Identity:** `git log -1 --format='%an <%ae> | %cn <%ce> | %G?' 715097ee91` gives "Yunus Shaikh <75296019+EagleBearerYS@users.noreply.github.com> | Yunus Shaikh <75296019+EagleBearerYS@users.noreply.github.com> | N", with no trailers.
- **Added comment lines:** 0.

## State

- The worktree `p095` is at 715097ee91 (local branch `prm/7642-9508-09cc865e29`), with the build directory current.
- No PR branch was touched, and nothing was posted.
