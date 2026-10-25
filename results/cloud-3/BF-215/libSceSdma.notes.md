# libSceSdma (26 todo: D 26)

**What it is:** the PS5 system DMA engine interface. It covers init/finalize and open/close, constant fill, linear, tiled and windowed (L2L, T2T, tiled) copies in blocking and non-blocking forms, queue mode (begin, kick, cancel, end), fences, flushes and event waits, and working-buffer size queries.

**Existing state:**
- 0 done. All 26 exports are parameterless pending declarations that throw (`core/libs/prx/libSceSdma/Export.cpp:10-138`).
- Names come from the PS5 3.20 export table and were cross-checked against firmware 13.60. Their signatures and behaviour are unverified (`docs/dev/debt/libSceSdma.md:5`).
- There is no CMakeLists or test. The library is built by the generic loop in `core/libs/CMakeLists.txt:159`.
- Related state: the PS4 `sceGnmSdma*` calls in `libSceGnmDriverForNeoMode` are also parameterless and only return `Failure` 0x8EEE00FF (`core/libs/prx/libSceGnmDriverForNeoMode/Export.cpp:50`, `:580-610`). They are a different library and NID set, so they answer nothing here.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 26.

## Families

- **D (26): unknown signature.** Every row is a parameterless pending-export declaration whose debt says signatures and behaviour are unverified.
  - Lifecycle: Initialize, Finalize, Open, Close and QueryWorkingBufferSize.
  - Copies and fills: ConstantFill, CopyLinear, CopyTiled, CopyWindowL2L, CopyWindowT2T and CopyWindowTiled, each with its NonBlocking form.
  - Queue and sync: BeginQueueMode, KickQueue, CancelQueue, EndQueueMode, FenceNonBlocking, FlushAll, FlushAllWithEvent, TryFlush and WaitForEvent.

## Batch proposals

- None: there are no A, B-S, B-M or C-real rows.

## Dependency notes

- Once prototypes are known, the linear copy and fill rows could be CPU copies on guest memory. The tiled and windowed rows need the GPU tiling modes the AGC layer models, and the queue, fence and event rows need a DMA queue model with kernel events. Sized together, that is a new subsystem (B-L).
