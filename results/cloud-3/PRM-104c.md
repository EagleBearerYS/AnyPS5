# PRM-104c: #7642, #9508's newest head merged into b5065a1fe9: stopped at step 4 (cloud-3)

Worker: cloud-3. **Stopped before committing, per step 4.** Four conflicts fall outside the files the task lets me resolve. One of them is the named stop: #9508's 866bd70c70 against #7642's recompiler change.
- No merge commit was made, and nothing was pushed: no `prm/7642-9508-*` branch, and no PR branch touched.
- The uncommitted merge is still in my local worktree.

## Summary

1. **#9508's head moved again:** **37c94cd099** (`37c94cd0991a8e417d054cf38123373c442c2563`) at 01:58Z.
   - It is a descendant of f2449f6ec2: "Merge main into perf/agc-vk-descriptor-heap", bringing in main 31034785f8 (#13812).
   - So I used it, as the task says.
   - #7642 is 06eba99c50 and b5065a1fe9 is unchanged, both as expected.
2. **The merge conflicts in 5 files.**
   - **`libSceAgcDriver/CMakeLists.txt`, 3 hunks: expected.** `tools/agc_test_split.py resolve` exits 0 and leaves no marker and no unmerged CMake file.
   - **Four C++ files, 5 hunks: each is a stop under step 3b or step 4:**

   | file | hunks | #7642's side (commit) | #9508's side (commit) | rule |
   |---|---:|---|---|---|
   | `core/shader/recompiler/ShaderDiskCache.cpp` | 2 | adds `deviceImageInterfaces` and `deviceSamplerClasses` to the invocation encode and decode (aef54d98b3) | **866bd70c70** drops the second, duplicate `userDataRegisterReads` encode and decode, right where #7642 appends | step 4's named stop: "866bd70c70 colliding with #7642's recompiler changes" |
   | `Execution/src/Driver/Dispatch/Dispatch.cpp` | 1 | adds the `deviceImageTables` / `DeviceImagesServe` line (aef54d98b3) | **675e23803d** rewrites `computeHeapKey` as `HeapFragmentKey(codeOffset, HeapKeyClass::Compute)` | step 4: a file outside the allowed set |
   | `Execution/src/Driver/Draw/DrawCapture.cpp` | 1 | the same `DeviceImagesServe` line (aef54d98b3) | **675e23803d** changes `profile` to `context.profile` on the next line | step 4: a file outside the allowed set |
   | `Graphics/src/ShaderResources.cpp` | 1 | `#include …/DeviceImageTable.hpp` (b9232e5980) | `#include …/MemoryAccounting.hpp`, #10194's 465aa0a238, through #9508's main merge | step 3b: the hunk isn't an allocate or free call, so it's a stop |

   - **No `FormatVersion` conflict:** it is 50 at the base, 62 on #7642 and 50 on #9508, so git merges it to 62 (§4).
3. **Each of the five hunks resolves mechanically by keeping both sides.** In every one, #7642's side is the base plus added lines, and #9508's side changes the base. §2 gives the exact proposed text. I applied none of it.
4. **Step 3's 22-name edit isn't needed at this head.**
   - #9508's own split merge removed the `agcGpuHeapTests` block and the `${agcDrawPipelineTests}` list.
   - It registers each `_gpu_heap` variant through a new `agc_add_gpu_heap_test()` (`CMakeLists.txt:636`), called from 78 fragments.
   - After `resolve`, all 22 draw pipeline tests have exactly one `agc_add_gpu_heap_test(<name>)`.
   - #7642 never changed that list: `set(agcGpuHeapTests …)` is identical at 31f5d0e8dd and b5065a1fe9.
5. **The other four #10194 files merge without conflict.** These are `PresentationPass.cpp`, `DepthSurface.cpp`, `Resources.cpp` and `SampleOrder.cpp`.
   - #7642 doesn't change them against 31f5d0e8dd. BF-350's hunks there were #9508's lines, which #7642 carries, and #9508 resolved them in its own main merge 936b81773f.
   - I left them as git merged them.
6. **The semantic check passes (§5).** No #7642 code assumes a draw's heap-ring data sits in one chunk.
   - #7642's trailer words go into the host-side map, and #9508's `heapSpan` copies that map into one span with one `memcpy`.
   - `BindlessHeap::Allocate` always returns a contiguous span.
   - The shader reads the trailer at flat ring address + span offset + index.
   - #7642's bucket table and miss ring are its own buffers, and its slot uploads go to the resource heap.
7. **The count, at configure level.** The C++ conflicts don't touch registration.

   | tree | tests |
   |---|---:|
   | (a) #9508's 37c94cd099 | 3,219 |
   | (b) b5065a1fe9 | 3,193 |
   | (c) the merge after `resolve` | 3,227 |

   - (c) is exactly (a) ∪ (b), and no name appears twice.
   - All 22 `_gpu_heap` variants are in (a), (b) and (c).
8. **The task's commit list is missing two of #9508's commits** since 31f5d0e8dd: **675e23803d** "refactor(agc): tidy the heap draw and dispatch paths" and **37878533aa** "refactor(agc): tidy the heap resource, pipeline and descriptor paths". 675e23803d causes the `Dispatch.cpp` and `DrawCapture.cpp` hunks.

**What I need:** a line allowing the five hunks to be resolved as in §2, or other rules. Everything after the merge is still to do: the build, the runs, the own diff, the checks, the commit and the push.

## 1. Start

| ref | value | check |
|---|---|---|
| `upstream refs/pull/9508/head` | **37c94cd0991a8e417d054cf38123373c442c2563** at 01:58Z | contains f2449f6ec2 (`merge-base --is-ancestor` true), 68 commits past it |
| `upstream refs/pull/7642/head` | 06eba99c5080f540d05b5c6227958206e54d7cdc | = 06eba99c50 |
| PRM-104b's merge | b5065a1fe9f8fa76287f1a0107b038bbdcee45c4 (`fork prm/7642-9508-31f5d0e8dd`) | as given |
| merge base of b5065a1fe9 and 37c94cd099 | 31f5d0e8dd | |

**37c94cd099's parents** are f2449f6ec2 and **31034785f8**: main's "Merge pull request #13812 from dr-hoseyn/fix/sampler-collapsed-thread-filter", 14 first-parent merges past cbcb37b314. It has neither 029463ceb6 nor today's main, 88b7503129.

**#9508's own commits in 31f5d0e8dd..37c94cd099** (not on main), oldest first:
1. 675e23803d "refactor(agc): tidy the heap draw and dispatch paths"
2. 37878533aa "refactor(agc): tidy the heap resource, pipeline and descriptor paths"
3. 866bd70c70 "refactor(shader): tidy the GPU heap recompiler paths"
4. 11ef9c57b1 "fix(agc): let one draw's shader data span several heap ring chunks"
5. dfd8a5eff9 "fix(agc): never wrap a heap ring span onto the current chunk behind its cursor"
6. f2449f6ec2 "test(agc): create the GPU heap before the memory accounting baseline"

It also has three main merges: 7b7d146541, 936b81773f and 37c94cd099.

**The command,** in my worktree on b5065a1fe9 (detached):
```
git -c rerere.enabled=false -c merge.conflictStyle=merge merge --no-ff --no-commit 37c94cd0991a8e417d054cf38123373c442c2563
```
- It reports conflicts in exactly the five files above.
- `git merge-tree --write-tree b5065a1fe9 37c94cd099` gives tree 677118656a, with the same five conflicted files.
- After `resolve`, my working tree differs from 677118656a only in:
  - `CMakeLists.txt`;
  - the four fragments `resolve` wrote;
  - the conflict-marker label lines of the four C++ files (`HEAD` against `b5065a1fe9`).

## 2. The four stop files: both sides, and the proposed resolution

Line numbers are at the uncommitted merge.

### 2.1 `core/shader/recompiler/ShaderDiskCache.cpp`: 2 hunks, the named 866bd70c70 stop

**Hunk 1, `encodeInvocation`, lines 703–708:**
```
<<<<<<< HEAD                                    (b5065a1fe9, #7642's aef54d98b3)
    writer.Value(invocation.userDataRegisterReads);
    writer.List(invocation.deviceImageInterfaces, [](Writer& out, std::uint64_t id) { out.Value(id); });
    writer.List(invocation.deviceSamplerClasses, [](Writer& out, std::uint32_t bits) { out.Value(bits); });
=======
>>>>>>> 37c94cd099                              (#9508's 866bd70c70: the line removed)
}
```

**Hunk 2, `decodeInvocation`, lines 755–760:** the same, with `reader.Value(invocation.userDataRegisterReads);`, `reader.List(invocation.deviceImageInterfaces, 8, …)` and `reader.List(invocation.deviceSamplerClasses, 4, …)`.

**The cause:**
- At the base 31f5d0e8dd, `encodeInvocation` writes `userDataRegisterReads` twice: at :680, and again as its last line at :700. `decodeInvocation` reads it twice too (:727, :747).
- 866bd70c70's message: "Drop the second encode/decode of userDataRegisterReads in the disk cache".
  - Its diff removes exactly lines 700 and 747.
- #7642 appends its two fields after that second line.

**Proposed:** keep 866bd70c70's removal and #7642's two fields, in both functions:
```
    writer.Value(invocation.imageTableFaults);
    writer.List(invocation.deviceImageInterfaces, [](Writer& out, std::uint64_t id) { out.Value(id); });
    writer.List(invocation.deviceSamplerClasses, [](Writer& out, std::uint32_t bits) { out.Value(bits); });
}
```
```
    reader.Value(invocation.imageTableFaults);
    reader.List(invocation.deviceImageInterfaces, 8, [](Reader& in, std::uint64_t& id) { in.Value(id); });
    reader.List(invocation.deviceSamplerClasses, 4, [](Reader& in, std::uint32_t& bits) { in.Value(bits); });
}
```

Encode and decode stay symmetric. It is a new layout under `FormatVersion` 62; §4 explains why that can't misread an old file.

### 2.2 `Execution/src/Driver/Dispatch/Dispatch.cpp`: 1 hunk, lines 192–199

```
<<<<<<< HEAD                                    (#7642's aef54d98b3 adds the first line; the other two are the base's)
    if (request.target.deviceImageTables) request.target.deviceImageTables = localDevice->DeviceImagesServe(address);
    constexpr std::size_t computeHeapKey = std::size_t{1} << 61u;
    if (request.target.gpuResolvedImages && (… || HeapFragmentKnown(&snapshot, codeOffset | computeHeapKey) == std::optional(false))) request.target.gpuResolvedImages = false;
=======                                         (#9508's 675e23803d)
    const std::size_t computeHeapKey = HeapFragmentKey(codeOffset, HeapKeyClass::Compute);
    if (request.target.gpuResolvedImages && (… || HeapFragmentKnown(&snapshot, computeHeapKey) == std::optional(false))) request.target.gpuResolvedImages = false;
>>>>>>> 37c94cd099
```

**Proposed:** #7642's added line, then #9508's two lines:
```
    if (request.target.deviceImageTables) request.target.deviceImageTables = localDevice->DeviceImagesServe(address);
    const std::size_t computeHeapKey = HeapFragmentKey(codeOffset, HeapKeyClass::Compute);
    if (request.target.gpuResolvedImages && (HeapRefused() || HeapStageExcluded(ShaderRecompiler::ShaderStage::Compute) || HeapFragmentKnown(&snapshot, computeHeapKey) == std::optional(false))) request.target.gpuResolvedImages = false;
```

At the base, :192–193 are exactly #7642's second and third lines, so #7642 only added the first.

### 2.3 `Execution/src/Driver/Draw/DrawCapture.cpp`: 1 hunk, lines 167–172

```
<<<<<<< HEAD                                    (#7642's aef54d98b3 adds the first line; the second is the base's :97)
    if (request.target.deviceImageTables) request.target.deviceImageTables = localDevice->DeviceImagesServe(program.binary.codeAddress);
    const auto waitedBefore = traceCapSync() || profile ? Graphics::Recorder::ThreadWaitedMs() : 0.0;
=======                                         (#9508's 675e23803d)
    const auto waitedBefore = traceCapSync() || context.profile ? Graphics::Recorder::ThreadWaitedMs() : 0.0;
>>>>>>> 37c94cd099
```

**Proposed:**
```
    if (request.target.deviceImageTables) request.target.deviceImageTables = localDevice->DeviceImagesServe(program.binary.codeAddress);
    const auto waitedBefore = traceCapSync() || context.profile ? Graphics::Recorder::ThreadWaitedMs() : 0.0;
```

### 2.4 `Graphics/src/ShaderResources.cpp`: 1 hunk, lines 14–18 (a #10194 file, step 3b)

```
<<<<<<< HEAD                                    (#7642's b9232e5980)
#include "prx/libSceAgcDriver/Graphics/include/DeviceImageTable.hpp"
=======                                         (#10194's 465aa0a238, through #9508's main merge)
#include "prx/libSceAgcDriver/Graphics/include/MemoryAccounting.hpp"
>>>>>>> 37c94cd099
```

**Why it's a stop:** step 3b allows only hunks that differ in an allocate or free call, and this one differs in an include. In this file, the merged tree has no Vulkan memory allocate or free call from either side (0 `vkAllocateMemory`, 0 `vkFreeMemory`, 0 `MemoryAccounting::` calls), so 3b has nothing to rewrite here.

**Proposed:** both includes, in the file's order, after `BufferPool.hpp` and before `Pipeline.hpp`:
```
#include "prx/libSceAgcDriver/Graphics/include/DeviceImageTable.hpp"
#include "prx/libSceAgcDriver/Graphics/include/MemoryAccounting.hpp"
```

## 3. The CMake conflict and the 22 `_gpu_heap` tests

**The conflict:** 3 hunks in `core/libs/prx/libSceAgcDriver/CMakeLists.txt`, at merge-tree lines 490, 1610 and 2010.
- In each, b5065a1fe9's side holds the pre-split registrations: 897, 341 and 255 lines.
- #9508's side is empty, because #12862's split moved those registrations to `tests/cmake/`.

**`python3 tools/agc_test_split.py resolve`** (the merged tree's copy) exits 0.
- It writes `CMakeLists.txt` and four fragments, which hold #7642's registrations:
  - `agc_device_image_keys_tests.cmake` (+9);
  - `agc_driver_device_image_path_tests.cmake` (+9);
  - `agc_driver_device_image_table_tests.cmake` (+7);
  - `agc_shader_memory_tests.cmake` (+6 −1, adding `agc_shader_table_trailer` and the device-image test names).
- No conflict marker is left in `CMakeLists.txt` or any fragment, and `git status` lists no unmerged CMake file.

**Step 3 (the 22 names) is moot.**
- At 37c94cd099, #9508 has no `agcGpuHeapTests` and no `agcDrawPipelineTests`.
- Its split merge added `function(agc_add_gpu_heap_test agcTestName)` (`CMakeLists.txt:636–641`). That function registers `${agcTestName}_gpu_heap` with the same environment the old block set.
- It also put an `agc_add_gpu_heap_test(<name>)` call in each fragment: 78 fragments in all.
- After `resolve`, each of the 22 `agc_add_draw_pipeline_test` names has **exactly 1** `agc_add_gpu_heap_test(<name>)`.
- #7642 never edited the old list: the `set(agcGpuHeapTests …)` line is token-identical at 31f5d0e8dd and b5065a1fe9. So there is nothing of #7642's to carry.

## 4. `FormatVersion` and the disk-cache layout

- **No conflict.** `ShaderDiskCache.hpp:16` is `FormatVersion = 50` at 31f5d0e8dd, 62 at b5065a1fe9 and 06eba99c50, and 50 at 37c94cd099 (main cbcb37b314 is also 50). Git takes 62.
- **866bd70c70 changes the invocation layout without a bump** on #9508's side, and after §2.1 the merged layout differs from #7642's own layout at 62.
- **Neither can misread an old cache file:**
  - The file header also carries `SourceVersion()`, and `Load` rejects any mismatch (`ShaderDiskCache.cpp:1498`).
  - `SourceVersion()` is a SHA-256 over a manifest of the recompiler's headers, CMake files and every compiled source of the target, so it includes `ShaderDiskCache.cpp` (`recompiler/CMakeLists.txt:188–196`, `ShaderCacheVersion.cmake:142`).
- I'd still keep 62 as git merged it, and I'm reporting this as a note, not as a stop.

## 5. Semantic check: does #7642 assume a draw's heap-ring data sits in one chunk?

**No.** These lines are at the uncommitted merge.

1. **#9508's ring (what 11ef9c57b1 changes).**
   - `BindlessHeap::Allocate(words, mark)` (`Graphics/src/BindlessHeap.cpp:546–568`) picks one `offset`.
   - If the span wouldn't fit before the ring's end, `wrapRingOffset` restarts it at 0 (:570–574); it never splits a span.
   - It returns `{offset, std::span(ringData + offset, words)}`: one contiguous span, whatever number of 2 MiB chunks it covers.
   - 11ef9c57b1 changes only `BindlessHeap.{hpp,cpp}` and `tests/BindlessHeapSlots.cpp`.
   - #7642 doesn't change `BindlessHeap.{hpp,cpp}`: `git diff 31f5d0e8dd b5065a1fe9` on them is empty.
   - *Corrected in PRM-104d:* this line first said #7642 touches none of the three files. In fact #7642 changes `tests/BindlessHeapSlots.cpp` (+2 −5): its 0f3e74aa41 moves the `APS5_HEAP_SLOT_CAP` setenv to before the device opens. That is test setup only and touches no ring code, so this check's conclusion stands.
2. **#7642's trailer words.**
   - `ShaderResources.cpp:6412–6422` (blamed to #7642's 85b3d403d9) builds a `Trailer` from the device image table and appends it to the host-side `map` with `TableAbi::AppendTrailer` (`ImageTableAbi.hpp:136–143`, four `push_back`s).
   - `heapSpan` (`ShaderResources.cpp:6494–6500`, raghav314's) then allocates `words.size()` and copies the whole map with a single `std::memcpy`.
   - So #7642's words are part of #9508's one span. No #7642 line computes a chunk index or compares a size with `RingChunkBytes`: its diff has 0 added lines naming `SpanMark`, `Held(`, `RingChunkBytes`, `heap.Allocate` or `heapSpan`.
3. **The shader's read of the trailer.**
   - `TableTrailerWord` (`SpirvDescriptors.cpp:113–116`, #7642's bbb446f55d) reads through `tableMapWord` (:83–87, #9508's).
   - `tableMapWord` reads through `EmitStageRingWordPointer` (`SpirvBufferAccess.cpp:100–104`): the ring's root address plus the stage's span offset, then `readOnlyWordPointer(base, index)`.
   - That is a flat 64-bit address plus a word index, with no chunk arithmetic. The trailer read is like any other map word's.
4. **The bucket table and miss ring are #7642's own buffers,** not #9508's ring.
   - `DeviceImageTable.cpp:100–102` creates `table` and `ring` (`RingChunks = 16` of `chunkStride`, `DeviceImageTable.hpp:85`).
   - `Trailer()` (:246–250) and `RingChunk()` (:252–257) address only those buffers.
   - Their chunking has nothing to do with `BindlessHeap::RingChunkBytes`.
5. **Slot uploads go to the resource heap.**
   - `HeapSlots` (`DeviceImageTable.cpp:88`) calls `BindlessHeap::TextureSlot` (:323).
   - That writes through `writeImageSlot` (:601–604) into `resourceHeap` at `slot * imageHeapStride`, not into the shader-data ring.

## 6. The count (configure level)

**Method:**
- Three trees, configured with CI's `CONFIGURE` flags plus `-DANYPS5_ENABLE_SPIRV_TOOLS=ON` and the Wild linker flags.
- Each list comes from `ctest --show-only=json-v1`:
  - (a) 37c94cd099, a fresh build directory;
  - (b) b5065a1fe9, a fresh build directory;
  - (c) the uncommitted merge after `resolve`.
- Each configure exited 0.
- Registration comes from CMake alone, and the four C++ conflicts don't change it.

**Results:**

| | (a) #9508 37c94cd099 | (b) b5065a1fe9 | (c) the merge |
|---|---:|---:|---:|
| tests | 3,219 | 3,193 | 3,227 |
| duplicate names | 0 | 0 | 0 |
| the 22 `_gpu_heap` variants | 22 | 22 | 22 |

- Every name in (a) is in (c), and every name in (b) is in (c). Nothing in (b) was dropped.
- **Only in (c) beyond (a) ∪ (b):** none. (c) = (a) ∪ (b), and 3,219 + 8 = 3,227.
- **The 8 names (b) adds beyond (a)**, #7642's own:
  - `agc_device_image_keys`;
  - `agc_driver_device_image_path`, `_gpu_path`, `_without_heap`;
  - `agc_driver_device_image_table`, `_heap`;
  - `agc_shader_device_image_tables`, `agc_shader_table_trailer`.
- (a) has 34 names that (b) lacks: #9508's and main's newer tests.

## 7. Not done, because of the stop

- the merge commit and the push;
- the own diff;
- the build;
- #7642's set ×10, the 31f5d0e8dd..37c94cd099 tests ×10 and `^(agc|shader)`;
- `check_test_registration.py` and `check_conventions.py`;
- the identity and comment-line checks.

They all need the committed merge. Nothing was built.

## Constraints

- Read-only. No commit and no push of any code branch, and nothing upstream or on a PR.
- The only pushes are this result and the board.
