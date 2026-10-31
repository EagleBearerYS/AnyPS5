# PRM-104b: #7642 (06eba99c50) merged with #9508's 31f5d0e8dd, with the two named edits

Worker: cloud-2. Base: #7642's head **06eba99c50**. #9508's head is **31f5d0e8dd**; `git ls-remote upstream` shows both at 20:22Z.

| Item | Value |
|---|---|
| Merge | **b5065a1fe9** (`b5065a1fe9f8fa76287f1a0107b038bbdcee45c4`), parents 06eba99c50 and 31f5d0e8dd, fork `prm/7642-9508-31f5d0e8dd` (pushed; `git ls-remote fork` reads b5065a1fe9). The PR branch is untouched and nothing was posted. |
| Command | `git -c rerere.enabled=false merge --no-ff 31f5d0e8dd` on 06eba99c50. cloud-4's saved patch is on cloud-4's machine, so I redid the merge from the refs. |
| Conflict | `docs/dev/ARCHITECTURE.md` only, resolved by keeping both sides (§1) |
| Edits | Exactly the two named ones: the `HeapRetention` edit and `texture.get()` at `DeviceImageTable.cpp:88` (§2) |
| Build | Full build with CI's Linux flags, `-k 0`: **exit 0**, 1727/1727 steps, 0 `FAILED`, 735 s. No other edit needed. |
| Own set ×10 | **"100% tests passed, 0 tests failed out of 60"**: 48 tests 10/10 (480 runs), 12 skipped |
| 653edfefa1..31f5d0e8dd tests ×10 | **"100% tests passed, 0 tests failed out of 34"**: 22 tests 10/10 (220 runs), 12 skipped |
| `^(agc\|shader)` once | **"100% tests passed, 0 tests failed out of 1284"**, 104 skipped. No known flake and no disk failure. |
| Checks | `check_test_registration.py build`: "1646 test executable(s), 0 built and not run as recorded, none new"; `check_conventions.py --base 31f5d0e8dd`: "All conventions checks passed." |
| Own diff | **Exactly the four expected differences** (§4) |
| Identity | author and committer `Yunus Shaikh <75296019+EagleBearerYS@users.noreply.github.com>`, unsigned (`%G?` = N), no trailers |
| Added comment lines | `git diff 31f5d0e8dd b5065a1fe9 \| grep -cE '^\+\s*//'`: **0**. Against 06eba99c50 also **0**. |

## 1. The conflict: `docs/dev/ARCHITECTURE.md`

**What conflicts:** the one `APS5_GPU_HEAP=1` bullet (line 146), which both sides edit.
- #9508 (653edfefa1) adds two sentences: "A heap slot holds no reference to its texture, … while a batch can still read it." and "With fewer free image or sampler slots than the margin, … twice the margin free." Its later commits 4243e9c4f1, dd83293174 and 31f5d0e8dd don't change the file.
- #7642 makes two word edits:
  - "the driver appends a sampled and a storage slot per element" becomes "the driver appends four trailer words (a device image table's bucket and miss ring addresses, zero without one) and then a sampled and a storage slot per element";
  - "(from `HeaderElementBase`)" becomes "(from `HeaderElementBase`, right after the trailer)".

**The resolution:** 31f5d0e8dd's line with #7642's two edits applied, as PRM-104 §1 says. Checks:
- Applying the two edits to the merge base's line gives 06eba99c50's line exactly.
- Each edit's old text occurs once in 31f5d0e8dd's line.
- The resolved file is byte-identical to PRM-102's resolved `ARCHITECTURE.md` in 9495b27d06 (`git diff 9495b27d06` on the file is empty).

## 2. The two edits

**1. `core/libs/prx/libSceAgcDriver/tests/execution/DeviceImageTable.cpp`, `HeapRetention`:**

```
-        BindlessHeap::NoteSweepDue();
-        BindlessHeap::SweepIfDue();
         table.Prepare(context, *recorder);
         for (std::size_t index = 0; index < keys.size(); ++index) {
             const auto value = table.Find(keys[index]);
-            Require(... == generations[index].second, "the heap's sweep keeps the slot of a texture the table holds");
+            Require(... == generations[index].second, "a texture the table holds keeps its heap slot");
```

The three remaining asserts are unchanged: `SlotTexture` against `EntryTexture`, the slot generations, and `Totals().swept == 0`. The function is identical to PRM-102's version in 9495b27d06.

**2. `core/libs/prx/libSceAgcDriver/Graphics/src/DeviceImageTable.cpp:88`, `HeapSlots`:**

```
-    slots.take = [heap](std::uint32_t kind, const std::shared_ptr<Texture>& texture, VkImageView view, VkImageLayout layout) { return heap->TextureSlot(kind, texture, view, layout); };
+    slots.take = [heap](std::uint32_t kind, const std::shared_ptr<Texture>& texture, VkImageView view, VkImageLayout layout) { return heap->TextureSlot(kind, texture.get(), view, layout); };
```

- `TextureSlot(std::uint32_t kind, Texture* texture, VkImageView view, VkImageLayout layout)` is at `Graphics/include/BindlessHeap.hpp:51` (31f5d0e8dd).
- `Slots::take` keeps its `std::shared_ptr<Texture>` parameter (`Graphics/include/DeviceImageTable.hpp:71`).

**The merge message** names both edits: "This keeps both sides; two edits: HeapRetention drops the deleted sweep calls; HeapSlots passes texture.get() to TextureSlot(Texture*) from dd83293174." It then has one paragraph each for `ARCHITECTURE.md`, `HeapRetention` and `HeapSlots`, in PRM-102's format.

## 3. Build and runs

**Setup:**
- Worktree `p095`, incremental from PRM-105's build of 06eba99c50.
- CI's Linux flags: Ninja, Release, gcc 13.3, ccache, `-DBUILD_TESTING=ON -DANYPS5_ENABLE_SPIRV_TOOLS=ON`.
- lavapipe (llvmpipe, LLVM 20.1.2, 256 bits), run as `runner` with `ANYPS5_REQUIRE_VULKAN=1 GALLIUM_OVERRIDE_CPU_CAPS=avx`.

**Build:**
- The merged tree with both edits, `cmake --build build -- -k 0`: **exit 0, 1727/1727 steps, 0 `FAILED`**, 735 s.
- It printed 51 warnings, which I traced back to the files that emit them:
  - 50 come from 26 files the merge doesn't change: mostly `tests/execution/*` shader tests, plus `PipelineLibrary.cpp`, `State.cpp`, `Locks.cpp`, `ConstantPhi.cpp` and `RequestSerializerTests.cpp`.
  - 1 is in `Graphics/src/DepthSurface.cpp`, at :1390: a `-Wformat` warning on `tileMode`. The merge does change that file, through 653edfefa1's `SampledElsewhere` hunk. But :1390 is identical on 06eba99c50, 31f5d0e8dd and the merge base, so the warning predates the merge.
  - None comes from `DeviceImageTable` or `BindlessHeap`.
- After committing, the incremental rebuild was exit 0 (730/730, relinks only).

**#7642's set ×10** (PRM-095 §6.3's 38 names plus `^agc_driver_heap_slots|^agc_driver_image_table|^agc_driver_device_image`, `--repeat until-fail:10 --timeout 600`):
- **"100% tests passed, 0 tests failed out of 60"**, 361.37 s. 48 tests passed 10/10 (480 runs), with 0 failed runs.
- It is 60 rather than PRM-102's 58 because dd83293174 registers two new `agc_driver_heap_slots` modes, `_owner_views_release` and `_shared_owner_release`.
- 12 skipped (a skip ends the repeat after one run):

| Tests | Reason (verbatim) |
|---|---|
| `agc_driver_heap_slots`, `_comes_back`, `_mixed_comes_back`, `_storage_comes_back`, `_storage_views_come_back`, `_views_come_back`, `_two_heaps_come_back`, `_owner_views_release`, `_shared_owner_release` (9) | "skipped, the device has no VK_EXT_descriptor_heap" |
| `agc_driver_device_image_table_heap` (the test with edit 1) | "skipped, the device lacks VK_EXT_descriptor_heap, which the GPU heap the device image table takes its slots from needs" |
| `agc_driver_device_image_path_gpu_path` | "skipped, the device lacks VK_EXT_descriptor_heap, which the GPU heap the device image path reads its tables through needs" |
| `agc_driver_image_table_store_gpu_heap` | "image table store skipped: per-dispatch image tables have no uint storage view (docs/dev/debt/libSceAgcDriver.md)" |

**Tests from the files 653edfefa1..31f5d0e8dd changes, ×10:**
- **Mapping,** as in PRM-102 §4: each changed test source goes to its executable through `build.ninja`, and each executable to its registrations through `CTestTestfile.cmake`. The five changed test files are:
  - `tests/BindlessHeapSlots.cpp` → `agc_driver_heap_slots_tests`: 9 tests;
  - `tests/execution/AddressDrawSpaceReuse.cpp` → `agc_driver_address_draw_space_reuse_tests`: 3 tests (`agc_driver_address_draw_space_reuse`, `_heap`, `_heap_transitions`);
  - `tests/execution/MultisampleRetirement.cpp` → `agc_driver_multisample_retirement_tests`: 1 test;
  - `tests/execution/SampledCacheBudget.cpp` → `agc_driver_sampled_cache_budget_tests`: 17 tests (the `agc_driver_sampled_cache_budget*`, `agc_driver_resource_cache_*` and `agc_driver_recover_*` tests, and `agc_sampled_cache_device_selection`);
  - `tests/execution/StorageCacheBudget.cpp` → `agc_driver_storage_cache_budget_tests`: 4 tests.
- **Result:** **"100% tests passed, 0 tests failed out of 34"**, 121.91 s. 22 tests passed 10/10 (220 runs), with 0 failed runs.
  - They include all three `agc_driver_address_draw_space_reuse` tests (31f5d0e8dd's multisample recheck), `agc_driver_multisample_retirement`, 16 of the 17 sampled-cache tests and the two storage-cache tests that run without the heap.
- **12 skipped, all "skipped, the device has no VK_EXT_descriptor_heap":** the 9 `agc_driver_heap_slots*` tests, `agc_driver_sampled_cache_budget_heap_view`, `agc_driver_storage_cache_budget_heap_slots` and `agc_driver_storage_cache_retained_batch_heap_slots`.
- **So lavapipe does not run dd83293174's new heap behaviour** (the shared pool, the owner release). The tests that cover it skip here, as PRM-102 §4 and PRM-104 §6 reported for the earlier heap changes.

**`^(agc|shader)` once, `-j4 --timeout 600 --output-on-failure`:**
- **"100% tests passed, 0 tests failed out of 1284"**, 151.17 s.
- Neither known flake (`agc_driver_idle_store_yields`, the `agc_driver_pipeline_journal` cold-compile timeout) failed, and there were no disk-space failures (9.4G free).
- 104 skipped. I reran each one once with `-V` to read its reason; every one is a device capability:

| Count | Reason |
|---|---|
| 39 | wave64 programs on lavapipe's narrower subgroups ("subgroup size N cannot hold a wave64", "the device's subgroups are narrower than a wave", "the masked pixel shader requires at least N subgroup lanes", and variants) |
| 24 | no `VK_EXT_descriptor_heap`, or the GPU heap that needs it: the 11 own-set heap skips above, the 3 cache-budget heap tests, and the `agc_driver_heap_draw_*`, `agc_driver_heap_compute_*`, `agc_driver_heap_table_stage_reuse`, `agc_driver_presentation_pass_gpu_heap` and `agc_driver_pipeline_journal_gpu_heap` tests |
| 11 | host imports or write watching unavailable (no `/dev/udmabuf`, no watched host-memory import, no deferred settle on Linux, no host memory type for the image) |
| 11 | "the device has no mesh shader compiler that handles these programs (llvmpipe)" |
| 9 | multisample colour attachments ("the device cannot render color with the target's sample count"), native FMASK included |
| 10 | one each: no pipeline cache; "per-dispatch image tables have no uint storage view" (`agc_driver_image_table_store_gpu_heap`); no conservative rasterization within 1/256 pixel; no native f16 with RTE rounding and kept denormals (`agc_driver_float16_native` and `agc_driver_packed_float16_native`); no `VK_EXT_sample_locations` at 4 samples; no subgroup operations in tessellation-evaluation shaders; no 2x colour and depth targets; no sampleable 2x depth targets; no executable statistics |

Compared with PRM-105's run of 06eba99c50 (1279 tests, 99 skipped), the merge adds 5 tests and 5 skips. The added tests are dd83293174's two `agc_driver_heap_slots` modes, 653edfefa1's `_two_heaps_come_back`, and its two storage-cache `_heap_slots` tests, all heap tests that skip here.

**Checks** (on b5065a1fe9):
- `python3 tools/check_test_registration.py build`: "1646 test executable(s), 0 built and not run as recorded, none new".
- `python3 tools/check_conventions.py --base 31f5d0e8dd`: "All conventions checks passed."

## 4. Own diff

**Totals:**
- **Before:** `git diff 0ab5f157a8 06eba99c50`: 56 files, +4146 −37, 4183 changed lines.
- **After:** `git diff 31f5d0e8dd b5065a1fe9`: 56 files, +4144 −37, 4181 changed lines.
- **The same 56 files** (identical `diff --git` header lists).

Comparing the sorted `+`/`-` lines leaves **exactly the four expected differences**, and nothing else:

| # | Difference | Only in before | Only in after |
|---|---|---|---|
| 1 | The `ARCHITECTURE.md` line's placement | the `-`/`+` pair against the merge base's line | the `-`/`+` pair against 31f5d0e8dd's line. Character-level, both new lines differ from the old ones only by #9508's inserted sentences, and #7642's two edits are identical in both pairs. |
| 2 | The two removed sweep lines | `+        BindlessHeap::NoteSweepDue();` and `+        BindlessHeap::SweepIfDue();` | (none) |
| 3 | The reworded message | `"the heap's sweep keeps the slot of a texture the table holds"` | `"a texture the table holds keeps its heap slot"` |
| 4 | `texture.get()` | `heap->TextureSlot(kind, texture, view, layout)` | `heap->TextureSlot(kind, texture.get(), view, layout)` |

The two removed sweep lines account for the 2-line difference in the totals.

## 5. Notes

- **The merge's own content:** `git show --stat b5065a1fe9` against its first parent is 32 files, +800 −841. That is #9508's three new commits plus 653edfefa1, with the resolution and the two edits.
- **PRM-104's semantic check** (cloud-4, §4) was made on this same tree: the uncommitted merge plus `.get()` at :88. Nothing in this merge differs from it. I did not redo that check, because PRM-104b doesn't ask for it.
