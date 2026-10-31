# PRM-106n: what the BDA cases run on, and why only Windows runs the failing check

Worker: cloud-4. Read at #12840's head **6bec085232** (`upstream refs/pull/12840/head`). Linux checks ran on this host (lavapipe, `ANYPS5_REQUIRE_VULKAN=1 GALLIUM_OVERRIDE_CPU_CAPS=avx ANYPS5_NO_SHADER_CACHE=1`, as `runner`).

**Scope.** PRM-106 is claimed by cloud-5 (16:10:45Z), and this note is addressed to its worker. I did only this note's "What to do first" step 1: read-only, no commits, no runs of #12840's code. PRM-106 itself, including the red, the T-or-P verdict and any commit, stays with cloud-5.

## Answer in short

1. **No device is involved.** The BDA cases run on a mock Vulkan context. The runner having no Vulkan device doesn't matter to them.
2. **The "falling back to copies" lines are not evidence of anything about the runner.** They come from cases that inject import failures on purpose, and a passing Linux run prints 43 of them.
3. **The failing case never runs on Linux.** `heapMirrorTests` needs a write-watched guest arena, which only the Windows arena provides. "It passes on Linux" means it is skipped there.
4. **The failing case attempts no host import on any platform.** Its context has `hostImportAlignment == 0`, so its heap mirror is always a copy.

So neither "the device fails host imports" nor "host imports fall back to copies" explains the Windows failure. The failing check (from #12840's 781dc4525b) has run only on Windows, and failed there the first time it ran. The question it raises stands unchanged: is a copy-backed writable heap mirror writable in the BDA table?

## 1. How `bdaContext` is made

- **The context:** `tests/Graphics.cpp:13022-13025`: `auto bdaContext = mockContext(); bdaContext.bufferDeviceAddress = true; bdaContext.limits.maxStorageBufferRange = 1u << 27; RunBdaResourceTests(bdaContext, …)`.
- **The mock:** `mockContext()` (`Graphics.cpp:5710-5723`) installs `mockProc` as the device's procedure table and one host-visible, host-coherent memory type. No `VkInstance` or physical device is created, so a runner without Vulkan runs these cases exactly as a Linux host does.
- **Its allocation function:** the mock's `vkAllocateMemory` (`Graphics.cpp:5347-5365`) fails an import in one case only: when a case sets `mock.importLimit` and the request carries an import struct. It then returns `VK_ERROR_OUT_OF_DEVICE_MEMORY` (-2, :5350). Otherwise it allocates a plain vector.
- **The other codes are injected by the BDA cases themselves:**
  - `BdaResources.cpp:944` sets `probe.failure = VK_ERROR_INVALID_EXTERNAL_HANDLE` (-1000072003);
  - `:4621` and `:4625` loop over `OUT_OF_HOST_MEMORY`, `OUT_OF_DEVICE_MEMORY`, `INVALID_EXTERNAL_HANDLE` and `UNKNOWN`.
- **The printed line:** "host import of … failed at %s (%d); falling back to copies" is printed by `GuestBufferMemory.cpp:2202` for any failed step.

**The same lines on Linux.** `agc_driver_graphics` on this host passes ("Graphics validation tests passed"). That binary carries main's BDA tests (73f4f7e06e), which have the same structure but lack the failing check. Its log still has **43** of these lines: `vkAllocateMemory` -2 ×19, -1000072003 ×10, -13 ×8 and -1 ×6. It also has 3 "on this device" notes. So the Windows failure output's import lines are the injected cases, not a Windows import path or a device limit.

## 2. Why only Windows runs the failing case

- **The gate:** `heapMirrorTests` (`BdaResources.cpp:2795-2799`) returns at once unless `GuestArenaAvailable_nid_postfix()` and `GuestArenaWriteWatched_nid_postfix()` both hold. `importedHeapMirrorTests` has the same gate (:2476-2478).
- **The flag:** the arena's `_writeWatched` is set only in its Windows constructor (`libc/src/GuestArena.cpp:192`: MEM_WRITE_WATCH unless `APS5_NO_WRITE_WATCH`). Elsewhere it stays `false` (:395).
- **On this host:** `agc_driver_graphics` prints `guest arena unavailable or not write-watched: heap mirrors not tested` and `… heap mirrors of imported ranges not tested`. Main has the same gate, so this holds at #12840's head too.
- **Why other heads pass on Windows:** the check at `BdaResources.cpp:2906` exists only on #12840 (781dc4525b is not in main). So the Windows sweeps of #7642, #13858 and main never had it to fail.

## 3. The failing case's mirror is a copy by construction

- **No import in this case:** `heapMirrorTests` gets `RunBdaResourceTests`' context unchanged (`BdaResources.cpp:4632`), which is `bdaContext`. `Context::hostImportAlignment` defaults to 0 (`Graphics/include/Context.hpp:181`), and `bdaContext` never sets it. The import paths return early on 0: `importsNow` at `GuestBufferMemory.cpp:3110`, the watch setup at :1266 and :1283.
- **The import variant is separate:** `importedHeapMirrorTests` sets `importing.hostImportAlignment = bytes` itself (`BdaResources.cpp:2489`), and it is equally Windows-only.

## 4. What this means for PRM-106

- **Neither suggested switch gives a Linux red.**
  - `APS5_NO_HOST_IMPORT` is read where a real `VulkanDevice` sets up imports (`Execution/src/VulkanDevice.cpp:1613`), which the mock context never reaches.
  - `APS5_HOST_IMPORT_MIB` only budgets imports this case never makes.
  - Either way the case stays skipped on Linux.
- **A Linux red needs a test seam that runs the heap-mirror case over memory Linux watches:**
  - Production builds heap mirrors over any watched range: `heapMirrorWatched` → `GuestMemory::Watched` (`GuestBufferMemory.cpp:3101-3107`). On Linux, `GuestMemory`'s watch is `GuestWriteWatch` (`Execution/src/GuestMemory.cpp:993-1001`: the Windows arena's watch under `_WIN32`, `GuestWriteWatchAvailable_nid_postfix()` elsewhere).
  - `GuestWriteWatch` is available on this host: none of the write-watch cases printed "write watch unavailable".
  - The seam: allocate the block with `mmap` and `GuestWriteWatchRegister_nid_postfix`, as `importedFreshTests` does (`BdaResources.cpp:2589-`), instead of `GuestArenaAllocate_nid_postfix`. Gate the case on `GuestWriteWatchAvailable` off Windows.
- **Not checked here: whether production takes the same path on Linux over that memory.** The Windows arena differs in more than the watch: it commits with `PAGE_READWRITE`, and the Windows-only parts of the import path are the `_WIN32` blocks at `GuestBufferMemory.cpp:2131-2155`, :2158-2163 and :2168-2174. So a Linux red with such a seam is likely but not certain; a green there would itself be a finding.
- **The T-or-P question is unchanged:** does a copy-backed writable heap mirror end up writable in the BDA table? On the evidence above it is the only platform-specific path in play. I did not answer it; PRM-106 owns it.
