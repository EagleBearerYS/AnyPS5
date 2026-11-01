# PRM-104d: #7642, finishing PRM-104c's merge: stopped at the start, #9508's head moved (cloud-3)

Worker: cloud-3. **Stopped at "Start", as the task says:** `upstream refs/pull/9508/head` is no longer 37c94cd099.
- I committed nothing and pushed no `prm/7642-9508-*` branch.
- PRM-104c's uncommitted merge (HEAD b5065a1fe9, MERGE_HEAD 37c94cd099, the four C++ files unmerged) is still in my worktree.

## Summary

1. **#9508's head at 02:18Z is 09cc865e29** (`09cc865e29c365a5b8152d68f2fb8f8d3f963872`). #7642 is still 06eba99c50, as expected.
   - 09cc865e29 descends from 37c94cd099 by four raghav314 commits, with no merge of main. In all, 48 files, +435 −551:
     - fc0cf9360f "refactor(agc): keep the heap walk memo's internals private";
     - 1cd3c3bdee "feat(agc): replay shader requests against a GPU heap target";
     - 0f4b636d04 "refactor(shader): share the GPU heap emitter helpers";
     - 09cc865e29 "test(agc): tidy the GPU heap tests".
2. **A dry run against the new head** (`git merge-tree --write-tree b5065a1fe9 09cc865e29`, tree 0fe5fc21f2, nothing committed) finds:
   - **PRM-104c's five C++ hunks, unchanged:** each hunk's text is byte-identical to the 37c94cd099 merge's. The three `CMakeLists.txt` hunks are there too.
   - **One new conflict:** `tests/BindlessHeapSlots.cpp`, 1 hunk (§2).
   - **One clean merge that wouldn't compile:** `SpirvDescriptors.cpp` (§3).
3. **The new `BindlessHeapSlots.cpp` hunk can't simply keep both sides.**
   - #9508's 09cc865e29 rewrites the device setup with `SetTestEnvironment` and `OpenVulkanTestDevice()`, and sets `APS5_HEAP_SLOT_CAP` after the device opens.
   - #7642's 0f3e74aa41 moved that setenv to before the device opens. Its message: with `APS5_GPU_PATH` on, the device constructor builds the heap, and `BindlessHeap` reads the cap once, in its constructor (`BindlessHeap.cpp:196`). So set after the open, the cap is never applied.
   - **Proposed:** #9508's helpers, with the cap set before `OpenVulkanTestDevice()` (§2).
4. **The `SpirvDescriptors.cpp` compile break.**
   - 0f4b636d04 removes the file-local `tableMapWord` and calls a shared `LoadTableMap` instead.
   - #7642's `TableTrailerWord` (`SpirvDescriptors.cpp:107–110` in the dry-run tree) still calls `tableMapWord` twice. Git merges this cleanly, but `tableMapWord` is defined nowhere in that tree.
   - **The fix** is to call `LoadTableMap` in those two places. That's a code edit outside any conflict hunk, so it needs a line from you (§3).
5. **PRM-104c has a wrong sentence, now corrected** in `results/cloud-3/PRM-104c.md` §5 item 1.
   - It said #7642 changes none of `BindlessHeap.{hpp,cpp}` and `tests/BindlessHeapSlots.cpp`. In fact #7642 changes the test file, through 0f3e74aa41, +2 −5.
   - The semantic check's conclusion stands: that change is test setup and touches no ring code.
6. **The semantic check still holds at 09cc865e29.**
   - `BindlessHeap.{cpp,hpp}` don't change in 37c94cd099..09cc865e29.
   - `heapSpan`'s `Allocate` and `memcpy` and `AppendTrailer` are unchanged (`ShaderResources.cpp:6422`, 6496–6497 in the dry-run tree).
   - `EmitStageRingWordPointer` is unchanged (`SpirvBufferAccess.cpp:106–110`).
   - `LoadTableMap`'s heap-mode branch is the same load the old `tableMapWord` did (§3).

**What I need:** one of these.
- **(a)** A PRM-104e on 09cc865e29: PRM-104c §2's five hunks, plus §2's `BindlessHeapSlots.cpp` resolution and §3's two-call edit.
- **(b)** Different rules.

## 1. Heads

| ref | at 02:18Z | expected | |
|---|---|---|---|
| `upstream refs/pull/9508/head` | **09cc865e29** | 37c94cd099 | **moved: stop** |
| `upstream refs/pull/7642/head` | 06eba99c50 | 06eba99c50 | ok |
| local merge state (`wt-bf164`) | HEAD b5065a1fe9, MERGE_HEAD 37c94cd099; unmerged: `Dispatch.cpp`, `DrawCapture.cpp`, `ShaderResources.cpp`, `ShaderDiskCache.cpp` | as PRM-104c left it | ok, kept |

**#9508's new files that #7642 also changes** (31f5d0e8dd..b5065a1fe9), 11 of them:
- `libSceAgcDriver/CMakeLists.txt`;
- `Execution/src/Driver/Draw/HeapStages.cpp`;
- `tests/BindlessHeapSlots.cpp`, `tests/ShaderMemory.cpp`;
- `Optimization/src/DescriptorBindingBuilder.cpp`;
- `SpirvEmitterState.hpp`, `SpirvBufferAccess.hpp`;
- `SpirvImageEmitter.cpp`, `SpirvBufferAccess.cpp`, `SpirvDescriptors.cpp`, `SpirvModuleEmitter.cpp`.

Only `CMakeLists.txt` and `BindlessHeapSlots.cpp` conflict in the dry run. The others merge cleanly, though `SpirvDescriptors.cpp` doesn't compile (§3).

## 2. The new conflict: `core/libs/prx/libSceAgcDriver/tests/BindlessHeapSlots.cpp`, 1 hunk

At the dry run's lines 393–422:
```
<<<<<<< b5065a1fe9                              (#7642's 0f3e74aa41 order: both settings, then the device)
#ifdef _WIN32
        _putenv_s("APS5_GPU_HEAP", "1");
        _putenv_s("APS5_HEAP_SLOT_CAP", comesBack ? "8" : "1");
#else
        setenv("APS5_GPU_HEAP", "1", 1);
        setenv("APS5_HEAP_SLOT_CAP", comesBack ? "8" : "1", 1);
#endif
        std::unique_ptr<AgcDriver::VulkanDevice> device;
        try {
            device = std::make_unique<AgcDriver::VulkanDevice>();
        } catch (const std::exception& error) {
            if (std::getenv("ANYPS5_REQUIRE_VULKAN") != nullptr) throw;
            std::printf("skipped, no usable Vulkan device: %s\n", error.what());
            return VulkanTestSkipped;
        }
        if (!device->GraphicsContext().descriptorHeap) {
            std::printf("skipped, the device has no VK_EXT_descriptor_heap\n");
            return VulkanTestSkipped;
        }
=======                                         (#9508's 09cc865e29: helpers, cap set after the device opens)
        SetTestEnvironment("APS5_GPU_HEAP", "1");
        auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        if (!device->graphicsContext().descriptorHeap) {
            std::printf("skipped, the device has no VK_EXT_descriptor_heap\n");
            return VulkanTestSkipped;
        }
        SetTestEnvironment("APS5_HEAP_SLOT_CAP", comesBack ? "8" : "1");
>>>>>>> 09cc865e29
```

**Why not "theirs":**
- 0f3e74aa41's message explains it: "With APS5_GPU_PATH on, the VulkanDevice constructor creates the device image table, whose HeapSlots call BindlessHeap::For and so build the device's heap. BindlessHeap reads APS5_HEAP_SLOT_CAP once, in its constructor, … so with the path on the heap kept its full slot count and none of the refusals at one or eight slots happened."
- At 09cc865e29, `BindlessHeap.cpp:196` still reads the variable in the constructor. #9508's order would undo #7642's fix.

**Proposed:** #9508's helpers, with #7642's order:
```
        SetTestEnvironment("APS5_GPU_HEAP", "1");
        SetTestEnvironment("APS5_HEAP_SLOT_CAP", comesBack ? "8" : "1");
        auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        if (!device->graphicsContext().descriptorHeap) {
            std::printf("skipped, the device has no VK_EXT_descriptor_heap\n");
            return VulkanTestSkipped;
        }
```

Two further notes:
- `OpenVulkanTestDevice()` (`tests/execution/VulkanTestDevice.hpp:34–44` at 09cc865e29) catches only `AgcDriver::VulkanUnavailable`, where the old code caught `std::exception`. That is #9508's change and applies the same either way.
- `GraphicsContext()` becomes `graphicsContext()` on #9508's side, and the proposal uses the new name.

## 3. The clean merge that breaks: `SpirvBackend/src/SpirvMemory/SpirvDescriptors.cpp`

- **0f4b636d04** removes `std::uint32_t tableMapWord(SpirvEmitterState&, std::uint32_t)` from the file's anonymous namespace.
  - Its four callers in the file become `LoadTableMap(state, …)`.
  - `LoadTableMap` is declared at `SpirvBufferAccess.hpp:25` and defined at `SpirvImageEmitter.cpp:1758–1769`.
- **#7642's `TableTrailerWord`** (bbb446f55d, at the dry run's `SpirvDescriptors.cpp:107–110`) calls `tableMapWord` twice.
  - Git merges the file without a conflict: 0f4b636d04's hunks and #7642's function don't overlap.
  - `git grep tableMapWord` on the dry-run tree finds only those two calls and no definition, so the merged tree doesn't compile.
- **The fix:** replace both calls with `LoadTableMap`:
  ```
  std::uint32_t TableTrailerWord(SpirvEmitterState& state, std::uint32_t field) {
      const auto base = LoadTableMap(state, ConstantU32(state, ImageTableAbi::HeaderElementBase));
      return LoadTableMap(state, Binary(state, spv::OpISub, TypeU32(state), base, ConstantU32(state, ImageTableAbi::TrailerWords - field)));
  }
  ```
- **What the fix changes in behaviour:**
  - `LoadTableMap`'s `state.TableMapAddressed()` branch is the old `tableMapWord` exactly: an `OpLoad` through `EmitStageRingWordPointer(state, RootStageField::TableMap, index)`.
  - Its other branch loads from `state.tableMapVariable` (an `OpAccessChain` into the table-map storage buffer), where the old helper always used the ring.
  - #9508's own callers in this file made the same switch in 0f4b636d04. With the fix, #7642's trailer read follows the same rule as the header and slot words it sits beside.
- This is an edit outside any conflict hunk, so PRM-104d's "the five resolutions and nothing else" doesn't cover it.

## 4. Not done

The commit, the push, the count for (c), the build, the runs, the disk-cache round trip and the checks. All of them wait on the decision above.

## Constraints

- No commit and no push of any code branch, and nothing upstream or on a PR.
- The dry runs were `git merge-tree` only.
- The only pushes are this result, PRM-104c's one-sentence correction and the board.
