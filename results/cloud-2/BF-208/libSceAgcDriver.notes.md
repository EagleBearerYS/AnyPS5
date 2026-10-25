# libSceAgcDriver (BF-208 classification)

- **What it is:** the AGC kernel-driver client that libSceAgc and titles call to submit command buffers, size and write driver packets, register workload streams and resources, and receive GPU events.
  - The tree's version also holds the PM4 executor and Vulkan backend (`Execution/`, `Graphics/`).
  - 84 done, 46 todo.
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 46.
- **Existing state (paths under `core/libs/prx/libSceAgcDriver/`):**
  - **Submission:** `Submit/src/Dcb.cpp`, `Acb.cpp` and `CommandBuffer.cpp`. `CommandBuffer.cpp` reads the queue context as KytyPS5 does (debt `:10`).
  - **Packet sizes:** `GetSetFlipPacketSizeInDwords`, `GetSetWorkloads{Active,Complete,StreamInactive}PacketSize`, `NopDataGetPacketSize` and `UserDataGetPacketSize` are done (`Dcb.cpp:45-67`). So are `WaitUntilSafeForRendering` (`:69`) and `NopDataWritePacket` (`Unimplemented.cpp:61`).
  - **Registration:** resource registration always fails with 0x8A6C9018. Workload streams are kept as a bitmask, ids 1–31 (`Resource/src/Registration.cpp:10-61`).
  - **State:** the TF ring is kept and `SetHsOffchipParam` keeps nothing (`State/src/Configuration.cpp`). The capture and trace predicates return false (`State/src/Status.cpp`).
  - **Retail constants:** debug, capture, printf, register, reset, suspend-point and user-data exports return retail constants (`Unimplemented.cpp:20-158`, debt `:33-34`).
  - **Events:** EOP events through `Eq/src/Event.cpp`.
  - The flip and rendering-wait packets are interpreted by `Execution/src/Driver/Queues/Submission.cpp` and `Driver/Packets/PacketExecution.cpp`.
- **The 46 todo:** all are parameterless throwing declarations.
  - 35 are in `Export.cpp:9-182`. None has a debt entry.
  - 10 are in `Unimplemented.cpp:163-219`, covered by debt `:14`, "signatures and behavior are unverified".
  - `sceAgcDriverGetHsOffchipParam` (`Unimplemented.cpp:54`, debt `:67`) has a ps5-vulkan signature, but the values' meaning is unknown.

## Why there is no A, B or C

**Driver-level packet writers (8):** `SetFlip`, `SetWorkloadsActive`, `SetWorkloadComplete`, `SetWorkloadStreamInactive`, `UserDataWritePacket` and `UserDataWrite{Push,Pop,Set}Marker`. These are the nearest to implementable.
- **Flip and workload packets:** libSceAgc already writes the same packets through a `CommandBuffer*`:
  - `sceAgcDcbSetFlip` (`libSceAgc/DcbState/src/Display.cpp:13`), 6 dwords;
  - `sceAgcDcbSetWorkloadsActive` (`Workload.cpp:41`), 18 dwords;
  - `sceAgcDcbSetWorkloadComplete` (`:30`), 12 dwords.
  - The driver's own size getters agree with those sizes (`Dcb.cpp:45-55`).
  - What is missing is the driver-level prototype: the raw command pointer and the argument order. The tree cites OpenAGC `include/agcdriver.h` only for `sceAgcDriverRegisterDefaultOwner` (debt `:5`). Under the no-guessed-signature rule these stay D.
  - Once a prototype is confirmed, these three are B-S writers over the existing libSceAgc packet code. They are tested in `guest_rendering_wait_tests`, which already checks the packet sizes.
- **`SetWorkloadStreamInactive`:** its layout is unknown even in libSceAgc (`Workload.cpp:60` throws).
- **The user-data family:** the driver's user-data packet is `payload + 7` dwords (`Dcb.cpp:31-35`). libSceAgc's markers are `payload + 1` NOPs (`libSceAgc/DcbState/src/Marker.cpp`), so the driver's format is not the one the tree knows.

## D families

- **Queues and submission variants (17):**
  - `CreateQueue`, `DestroyQueue`, `SetupAsyncGraphics`;
  - `SubmitMultiCommandBuffersDirect`, `SubmitReprojectionAcb`, `SuspendPointSubmit`/`Direct`/`Cdbg`, `IDHSSubmit`, `TmpInitIdhs`;
  - Razor and HDR-scopes ACQ acquire, submit and release (5), and CWSR suspend/resume (2).
  - Their arguments are private. `IsSuspendPointInFlight` returns the retail 0x8A6D0001, but the submit exports are not among the retail constant bodies (debt `:34`).
- **Driver and kernel state (14):**
  - register shadowing (`GetRegShadowInfo`, `GetRegShadowInfoAgr`, `SetupRegisterShadow`); the executor keeps no shadow memory;
  - `PatchClearState` and `NotifyDefaultStates`; CLEAR_STATE itself is executed at `Execution/src/Pm4.cpp:300`;
  - `PassInfoDownward`: libSceAgc's `{7, 8, 0xb}` keys are in libSceAgc debt `:43`, but the kernel consumer is private;
  - `ModuleRegistration`, `GetReservedDmemForAgc`, `GetWorkloadStreamInfo`, `SetValueToSharedGds`, `SetHsOffchipParamDirect`, `GetHsOffchipParam`, `Hp3dLockStart`/`End`.
- **System and debug queries (7):** `DebugHardwareStatus`, `GetPaDebugInterfaceVersion`, `IsEsvmProcess`, and `Sys{EnableSubmitDone45Exception,GetClientNumber,IsGameClosed,GetGfxAppGpuResetStatus}`. These have no measured console answer.

## Batch proposals

None: no A/B/C family.

If the three flip and workload writers become B-S once a prototype is confirmed, they are a 3-function PR, under the batch floor:
- Tests extend `guest_rendering_wait_tests` (`core/libs/tests/GuestRenderingWait.cpp`, `core/libs/prx/libSceAgcDriver/CMakeLists.txt:3551`, `add_test(NAME guest_rendering_wait …)` at `:3555`).
- Byte-compare each driver writer against the `sceAgcDcb*` packet for the same arguments, and check the returned advance against the size getters.

Other guest executables:
- `guest_agc_driver_resources` (`:3533`/`:3537`);
- `guest_agc_driver_debug_unavailable` (`:3542`/`:3546`);
- `agc_driver_nop_data_packet` (`:470`/`:474`).

## Caveats

- 35 of the 46 todo (`Export.cpp`) have no entry in `docs/dev/debt/libSceAgcDriver.md`. The `Unimplemented.cpp` ones are covered by `:14`.
- GPU behaviour: no row changes the AGC executor, the shader recompiler or VideoOut.
- The tree already knows the PM4 and flip packet bytes. Every D in the writer family is a prototype gap, not a format gap, except `SetWorkloadStreamInactive` and the user-data family.
