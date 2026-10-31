# PRM-112b: #12862 split wave, #12840 again (cloud-1)

**#12840 merged, checked, built, tested and pushed.**
- **Main:** 0836dfdf4f (the wave's main).
- **The merge:** **9d2c13fd78** on d22d9c1a11, pushed to `upkeep/12840-split-0836dfdf4f`.
- **The eviction check:** clean. #13581's eviction can't drop a mirror with #12840's pending stores (lines below).
- **Not touched:** no PR branch (`upstream/pr/12840` is still 6bec085232), and nothing was posted.

## The table

| PR | old head | new (merge) | upkeep branch | conflicted | fragment changed (+/−) | own diff (old / new lines) | `ctest -N` | the PR's tests |
|---|---|---|---|---|---|---|---|---|
| **#12840** | d22d9c1a11 (on 6bec085232) | **9d2c13fd78** | `upkeep/12840-split-0836dfdf4f` | `libSceAgcDriver/CMakeLists.txt` (2 hunks), `Graphics/include/GuestBufferMemory.hpp` (1 hunk) | `agc_driver_bda_store_heap_mirror_tests.cmake` +26 −0 (new file); `agc_driver_pipeline_journal_tests.cmake` +4 −0 | 2014 / 2019; the move's block lines only (below) | **Total Tests: 3080** | `agc_driver_bda_store_heap_mirror`, `_budget`, `_no_space_cache`, `_store_pages`, `_store_pages_default`: 1 each; `agc_driver_pipeline_journal` changed (TIMEOUT 240) |

**The refs, checked first:**
- `upstream/pr/12840` = 6bec085232.
- `prm/12840-heap-mirror-case` = d22d9c1a11, whose only parent is 6bec085232.
- `upkeep/12840-split-0836dfdf4f` did not exist yet.

**The merge commit:**
- Parents d22d9c1a11, then 0836dfdf4f.
- Author and committer `Yunus Shaikh <75296019+EagleBearerYS@users.noreply.github.com>`, `%G?` = N, no trailers.
- The task's message, byte for byte.
- `git ls-remote origin` shows the upkeep branch at 9d2c13fd78.

## The merge (steps 1–4)

**Step 1:** `git checkout --detach d22d9c1a11`, then `git -c rerere.enabled=false merge --no-ff --no-commit 0836dfdf4f`. It conflicted in exactly the two expected files:
- `core/libs/prx/libSceAgcDriver/CMakeLists.txt`, 2 hunks;
- `core/libs/prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp`, 1 hunk.

Every other file merged cleanly.

**`GuestBufferMemory.hpp` (step 3), keep both:**
- Main's 4 lines come first: `HoldsMirror`, `EvictMirror`, `WasEvicted`, `RecordEvicted`.
- Then #12840's 15 lines: `AllowAddressStores`, `NoteAddressStores`, `struct StoreSpan`, `struct StorePage`, `AddressStoreSpans` and `NoteStorePages`.
- Both sides byte for byte, now at lines 337–355. No other line changed, and the file is `git add`ed.

**`resolve` (step 4):** `python3 tools/agc_test_split.py resolve` printed `CMakeLists.txt`, `tests/cmake/agc_driver_bda_store_heap_mirror_tests.cmake` and `tests/cmake/agc_driver_pipeline_journal_tests.cmake`, and exited 0. Afterwards:
- `git ls-files -u` was empty;
- no changed file has a `<<<<<<<`, `=======` or `>>>>>>>` line.

## The checks

**Nothing else changed.**
- `git merge-tree --write-tree d22d9c1a11 0836dfdf4f` gives git's own merge, tree 3da8972108.
- `git diff --name-only 3da8972108 9d2c13fd78` lists 4 files: `libSceAgcDriver/CMakeLists.txt`, `Graphics/include/GuestBufferMemory.hpp`, `tests/cmake/agc_driver_bda_store_heap_mirror_tests.cmake` and `tests/cmake/agc_driver_pipeline_journal_tests.cmake`.

**The own diff.**
- **Method:** sorted `+`/`−` lines of `git diff d30e04fb8a d22d9c1a11` (merge base d30e04fb8a) against those of `git diff 0836dfdf4f 9d2c13fd78`. That gives 2014 and 2019 lines.
- **Nothing is only in the old diff.** Every line of the PR's own change is in the new one, including:
  - its 25 registration lines (24 in the new fragment, 1 in the journal fragment), byte for byte, indentation included;
  - its 15 `GuestBufferMemory.hpp` lines;
  - its `CMakeLists.txt` build rules, unchanged and still in `CMakeLists.txt`: the `StorePageGather.comp` custom command (12 lines plus its blank line) and `${agcStorePageGatherShaderHeader}` in `agcDriverSources`.
- **Only in the new diff: 5 lines,** the wrappers `resolve` writes.
  - The new fragment's `if(BUILD_TESTING)` … `endif()`.
  - In `agc_driver_pipeline_journal_tests.cmake`, a blank line plus `if(BUILD_TESTING)` … `endif()` around the PR's `set_tests_properties(agc_driver_pipeline_journal PROPERTIES TIMEOUT 240)`.

**The count check.**
- I configured with `cmake -S . -B build-check -G Ninja -DBUILD_TESTING=ON`, as PRM-109 did, at each commit's submodule pins. Exit 0, no CMake error or warning.
  - **9d2c13fd78: `Total Tests: 3080`. 0836dfdf4f: `Total Tests: 3075`.**
- `ctest --show-only=json-v1`, comparing name, command and properties:
  - no name appears twice in either list;
  - none of main's 3075 tests is missing;
  - **5 added:** `agc_driver_bda_store_heap_mirror`, `agc_driver_bda_store_heap_mirror_budget`, `agc_driver_bda_store_heap_mirror_no_space_cache`, `agc_driver_bda_store_heap_mirror_store_pages`, `agc_driver_bda_store_heap_mirror_store_pages_default`;
  - **1 changed:** `agc_driver_pipeline_journal`. Its command is the same and its TIMEOUT goes from unset to 240 (#12840's own change, BF-349 §2.2).
  - Every other test of main's keeps main's command and properties.
- `build-check` was deleted.
- In the CI-flags build directory (`ANYPS5_ENABLE_SPIRV_TOOLS=ON`, which registers 12 more), the same pair gives 3087 and 3092: +5 again.
## The eviction check (step 5)

**The question:** can #13581's eviction drop or free a heap mirror, or its spans, after #12840 records pending stores for it and before the `WriteBack` that consumes them?

**Answer: no.** A mirror that carries pending stores is always held by the `GuestBufferMemory` that recorded them, and eviction never takes a held writable mirror. Line numbers are at the merged tree 9d2c13fd78, in `core/libs/prx/libSceAgcDriver/Graphics/src/GuestBufferMemory.cpp` unless another file is named.

### 1. #12840's pending state, and where it is consumed

**Spans are recorded only for writable heap mirrors, at upload.**
- `UploadFinish` (5608) calls `addAddressStoreSpans()` at 5827.
- `addAddressStoreSpans` (6544) records a span only if `region.mirror != nullptr && region.mirror->heap && region.mirror->writable && region.writable` (6548).
- It walks both the shared address space's regions, `space->base` (6557–6558), and the memory's own `regions` (6560).

**`AddressStoreSpans` (6531–6542)** builds the `StoreSpan`s, device addresses included, from `region->mirror` (6535–6538). `BdaResources.cpp:127` reads them.

**The stored pages are plain data.**
- `NoteAddressStores` (6522–6524) and `NoteStorePages` (6526–6529) fill `storedPages` and `storePageEntries` and take no reference to a mirror.
- `ShaderResources::WriteBackBuffers` notes them (`ShaderResources.cpp:6030–6031`), then calls `guestMemory.WriteBack()` on the same object (`ShaderResources.cpp:6033`).

**`WriteBack` (6629) consumes them through the same regions:**
- `owner()` (4873) searches `regions` and `space->base`;
- `storeRuns` needs `region.mirror` writable and heap (6639);
- it writes through `*region.mirror` (6653–6658, 6694–6736);
- only at its end does it release `space` (6796) and clear `storedPages`/`storePageEntries` (6800–6801).

### 2. The memory holds those mirrors the whole time

**The references are owning:**
- `Region::mirror` is a `std::shared_ptr<ImageMirror>` (`GuestBufferMemory.hpp:465`);
- the memory's `space` is a `std::shared_ptr<const AddressSpace>` (`GuestBufferMemory.hpp:562`), whose `base` regions hold their mirrors the same way.

**Nothing drops a region's mirror between 5827 and 6796.** Every place that clears `region.mirror` runs before upload ends or at `WriteBack`'s end:
- 4440 (`EvictMirror`, see §4);
- 5014 (`bindSparseMirror`, registration);
- 5456 and 5505 (`UploadPrepare`);
- 5655 (`UploadFinish`, an expired sub-range mirror, before 5827);
- 6796 (`space.reset()` at `WriteBack`'s end).

`dissolveSpace` (4844) runs at 4921, 4936, 5028, 5051 (registration) and 5687 (`UploadFinish`, before 5827). It copies `space->base` into `regions` (4848–4856), so the references move rather than drop.

### 3. Eviction takes only mirrors that nothing else holds

`sweepMirrors` (3313) is the only place that evicts.
- **Candidates:** `while (state.heapBytes + neededBytes > heapMirrorBudget())` (3349) picks only `heap` mirrors (3352) that pass `it->second.use_count() == allowedCount` (3355).
- **The allowance:** `allowedCount` is 2 only when the building memory holds the mirror and it is **not writable**, and 1 otherwise (3353–3354).
- So **a writable mirror is evicted only when `Mirrors().entries` holds the one reference.** A memory with pending stores for it holds at least one more, through `regions` or `space->base` (§2). That mirror never passes 3355.
- **Also kept out:** the address being reserved or grown (`protectAddress`, 3352), and mirrors with queued refresh blocks (`queued`, 3345–3347 and 3352, from ed3b5393f1).
- **What eviction does to a victim:** `unlistMirror`, erase and `dropped` (3374–3376), with the last reference released when `dropped` goes out of scope. That reference is the only one there is.
- **The entry points:**
  - `reserveHeapBytes` (2649–2650) and `growResident` (2671–2672), reached from `acquireMirror` (3212, 3230), `bindHeapBlocks` (2899) and `takeHeapReferences` (6577; `UploadFinish` 5828, after 5827);
  - `AcquireRegistered`'s final `sweepMirrors()` (4803), which has `neededBytes == 0` and so skips the eviction loop (3344).
  - **The guard at 3355 is the same at all of them, on any thread.**

### 4. `EvictMirror`, `HoldsMirror`, `WasEvicted` and `RecordEvicted` touch only a memory that has nothing pending

- **They are called only on `buildingBufferMemory`** (3353, 3368–3372). That is a thread-local (149) set only by `AcquireRegistered`'s `BuildingScope` (4465–4472) and cleared when it ends.
- **`AcquireRegistered` requires a fresh memory:** `Require(!uploaded && regions.empty() && lease.empty() && space == nullptr, …)` (4473). So the memory being built has no store spans (those come at `UploadFinish` 5827) and no noted pages (those come after its GPU work, `ShaderResources.cpp:6030`).
- **`EvictMirror` (4436–4454)** only resets that memory's own `region.mirror` and makes the region host-backed or copy-backed, before its upload.
  - For a writable mirror, `allowedCount` is 1 (3354), so a mirror the building memory holds can't be a victim at all.
  - When it is a non-writable victim, that region could never carry a store span (6548).
- **`RecordEvicted`/`WasEvicted` (4456–4462)** keep the building memory from mirroring an evicted range again in the same build (4657–4658). Such a region has no mirror, so it records no store span (6548). `WriteBack` stores it through the region's own buffer (6774, `storeChanged` at 6779), as for any copied region.

### 5. What else erases mirror entries

**These predate #13581, and none frees a held mirror:**
- `sweepMirrors`'s expired-range drop (3330–3343; #13581 only moves it into `dropped`, 3340);
- `releaseImportedMirrors` (3385–3420).

Both only erase the `Mirrors()` cache entry. A memory that still holds the `shared_ptr` keeps the object, and with it the buffer and device address its spans use.

## Build and test (step 7)

**The environment:**
- the merge 9d2c13fd78;
- Ubuntu 24.04, gcc 13.3, Release, Ninja, ccache;
- lavapipe (llvmpipe, LLVM 20.1.2, Mesa 25.2.8);
- tests run as the unprivileged user `runner` with `ANYPS5_REQUIRE_VULKAN=1 GALLIUM_OVERRIDE_CPU_CAPS=avx`.

**#13581's tests:** the only registration for `tests/execution/ResidentHeapMirror.cpp` is **`agc_driver_resident_heap_mirror`**.
- It is the execution test's default registration (`agc_add_default_test`).
- It runs with `ANYPS5_NO_SHADER_CACHE=1;APS5_NO_PIPELINE_JOURNAL=1`.
- No fragment adds another.

**#12840's set** (`^agc_driver_pipeline_journal|^agc_pipeline_journal|^agc_driver_bda|^agc_driver_graphics$`) is these 16 tests:
- `agc_driver_bda_anchor_reads`, `agc_driver_bda_span_reads`, `agc_driver_bda_span_reads_draw_pipeline`;
- `agc_driver_bda_store_heap_mirror` and its `_no_space_cache`, `_budget`, `_store_pages`, `_store_pages_default` variants;
- `agc_driver_bda_device`, `agc_driver_bda_device_selection_cpu`, `agc_driver_bda_reads_shadowed_results`, `agc_driver_bda_reads_shadowed_results_per_build`;
- `agc_driver_pipeline_journal`, `agc_pipeline_journal`, `agc_pipeline_journal_long_paths`;
- `agc_driver_graphics`.

| # | run | result |
|---|---|---|
| 1 | **build, `ANYPS5_ENABLE_SPIRV_TOOLS=ON`** (CI's flags): `ninja -C build` | **exit 0**, 3470 steps, no `FAILED:` |
| 2 | `tools/check_test_registration.py build` | `1647 test executable(s), 0 built and not run as recorded, none new` |
| 3 | `tools/check_conventions.py --base 0836dfdf4f` (head 9d2c13fd78) | `All conventions checks passed.` |
| 4 | **#12840's set ×10**, `--repeat until-fail:10 --parallel 4` | **16/16 passed, 160 runs, 0 failed, 0 skipped**, 251 s |
| 5 | **#13581's `agc_driver_resident_heap_mirror` ×10**, `--repeat until-fail:10` | **1/1 passed, 10 runs**, 7.9 s |
| 6 | **`^agc` once**, `--parallel 4` | **1158 tests: 1083 passed, 75 skipped, 0 failed**, 150 s |
| 7 | **build, `ANYPS5_ENABLE_SPIRV_TOOLS=OFF`**: `ninja -C build-a` | **exit 0**, 5992 steps, no `FAILED:`, 8 min 10 s |
| 8 | `tools/check_test_registration.py build-a` | `1642 test executable(s), 0 built and not run as recorded, none new`; `ctest -N`: **Total Tests: 3080** |
| 9 | **#12840's set ×3**, OFF build, `--repeat until-fail:3 --parallel 4` | **16/16 passed, 48 runs**, 78 s |
| 10 | **#13581's test ×3**, OFF build | **1/1 passed, 3 runs** |

**Skips in run 6:**
- 75 tests are reported `Skipped` by their own skip code or skip expression, for example `agc_driver_bitfield_mask`, `agc_driver_conservative_rasterization`, `agc_driver_dpp16_*` and `agc_host_import_sparse`.
- None is in #12840's or #13581's set.
- No test failed, so nothing needed a rerun without `GALLIUM_OVERRIDE_CPU_CAPS`.

## Push (step 8)

Steps 5 and 7 were clean, so 9d2c13fd78 went to the fork as `upkeep/12840-split-0836dfdf4f`. Nothing else was pushed.

## State

- The clone is detached at 9d2c13fd78, clean.
- `build` and `build-a` hold this merge's builds.
- `build-check` was deleted.
