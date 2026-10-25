# libSceVrTracker (21 todo: A 0, B-S 1, B-M 0, B-L 0, C-real 1, C-silent 3, D 16)

**What it is:** the PS VR (first generation) camera-based tracker: memory query, init and term, device registration, GPU submission and wait, CPU processing, result and time queries, and calibration and orientation resets (sysmodule `0x000000ed`, `core/libs/prx/libSceSysmodule/ModuleTable.hpp:123`).

**Existing state** (`core/libs/prx/libSceVrTracker/Export.cpp`):
- Built: no `CMakeLists.txt` of its own, so `core/libs/CMakeLists.txt:159` compiles `Export.cpp`, and `:170` links libkernel.
- 9 done: QueryMemory (`:131`), Term (`:209`), the three RegisterDevice calls (`:154-168`), UnregisterDevice (`:213`), Recalibrate (`:146`), SetDurationUntilStatusNotTracking (`:185`) and GpuWaitAndCpuProcess (`:124`).
  - They follow shadPS4 at a808e76, a PS4 implementation: its layouts, memory sizes, argument checks and one handle per device type (debt `:13`). Nothing is measured on a PS5.
- State: `g_initialized` (`:60`) and `g_deviceHandles[4]` behind `g_devicesMutex` (`:65-66`).
- Error constants (`:30-35`): NotInit 0x81260801, AlreadyInitialized 0x81260802, DeviceNotRegistered 0x81260803, DeviceAlreadyRegistered 0x81260804, ArgumentInvalid 0x81260806, NotExecuteGpuSubmit 0x81260815.
- No camera, so no GPU submission: GpuWaitAndCpuProcess answers NOT_EXECUTE_GPU_SUBMIT once initialized (`:125`, debt `:13`).
- `sceVrTrackerInit` (`:92-107`) is counted todo but is mostly done. The 128-byte parameter is validated and accepted; only a 144-byte parameter throws (`:94`).
- Record sizes recovered: 32-byte CPU and GPU-wait records, 56-byte result queries, 64-bit time, 552- or 648-byte submissions, 1520- or 3640-byte results (debt `:5`; `static_assert`s `:9-24`). Most fields are unresolved.
- **Test:** `guest_vr_tracker_devices_tests` (`core/libs/tests/GuestVrTrackerDevices.cpp`), built at `core/libs/CMakeLists.txt:494` and registered at `:497` (`add_test(NAME guest_vr_tracker_devices ...)`).
  - It asserts the before-init answers (`:97-104`), init validation and the 144-byte throw (`:106-134`, the throw at `:111`), and device registration.
  - The census input lists no tests for this library: the registration lives in the central `core/libs/CMakeLists.txt`, not in a library `CMakeLists.txt`.

## Families

- **B-S (1): `sceVrTrackerGetTime`** (`:80`, `uint64_t*`; debt `:5`: time queries write 64 bits).
  - Semantics from shadPS4 a808e76 (https://github.com/shadps4-emu/shadPS4/blob/a808e76/src/core/libraries/vr_tracker/vr_tracker.cpp#L222-L232), the source the done calls follow:
    - NOT_INIT before init, as the siblings (`:147`, `:161`);
    - ARGUMENT_INVALID for null;
    - otherwise write `sceKernelGetProcessTime()`, which is in the tree (`core/libs/prx/libkernel/Time/Time.cpp:168`: microseconds since process start).
- **C-real (1): `sceVrTrackerGpuWait`** (`:88`):
  - NOT_INIT before init, then NOT_EXECUTE_GPU_SUBMIT, the camera-less answer its done sibling GpuWaitAndCpuProcess already gives (`:124-126`, debt `:13`).
  - A null param, or a size other than 32 (`include/Api.hpp:45-48`, `static_assert` `:19`), gives ARGUMENT_INVALID, as in shadPS4 (`vr_tracker.cpp#L244-L255`).
- **C-silent (3), input-only calls with nothing to act on; each needs a title:**
  - `sceVrTrackerNotifyEndOfCpuProcess` (`:127`): a 32-byte notice of size and reserved words (https://github.com/shadps4-emu/shadPS4/blob/f9d07487dd85bb7dc522df6de109b8ad4b9b7175/src/core/libraries/vr_tracker/vr_tracker.h#L226-L229). No CPU process ever runs here.
  - `sceVrTrackerResetOrientationRelative` (`:173`, `(int, int)`): no orientation is tracked.
  - `sceVrTrackerUpdateMotionSensorData` (`:225`): size, device type, mode and handle (`vr_tracker.h#L231-L237`). Nothing consumes motion data.
  - shadPS4 returns 0 for all three with no checks (`vr_tracker.cpp#L267-L271`, `#L323-L327`, `#L363-L367`). That is a stub, not a console answer. Their parameter shapes come from that header (debt `:8`, `:9`).
- **D (16):**
  - No prototype (11): CpuPopMarker, CpuPushMarker, GetLiveCaptureId, ResetAll, SaveInternalBuffers and SetDeviceRejection (debt `:6`); SetExtendedMode, SetLEDBrightness, SetRestingMode, StartLiveCapture and StopLiveCapture (debt `:7`). shadPS4 also declares them without parameters (`vr_tracker.h#L377-L402`). GetLiveCaptureId has to hand back an id somehow.
  - `sceVrTrackerCpuProcess` (`:72`): its combined form fails here with NOT_EXECUTE_GPU_SUBMIT. What CpuProcess alone answers with no submission is unmeasured, and mode and argument are unresolved (debt `:5`). A success would contradict the combined call.
  - `sceVrTrackerGetResult` (`:76`): the result records are unresolved (debt `:5`), and the no-tracking answer is unmeasured.
  - `sceVrTrackerGetPlayAreaWarningInfo` (`:120`): an output record whose layout exists only in shadPS4's PS4 header (`vr_tracker.h#L344-L355`). shadPS4 writes nothing (`vr_tracker.cpp#L211-L214`).
  - `sceVrTrackerGpuSubmit` (`:84`): shadPS4 answers ARGUMENT_INVALID for every initialized call, reasoning that camera frames cannot be valid (`vr_tracker.cpp#L234-L242`). That is an inference, not a measurement, and the tree has no answer of its own for a submission. `sceCameraGetFrameData` is itself todo (`core/libs/prx/libSceCamera/Export.cpp:56`).
  - `sceVrTrackerInit`: only the 144-byte branch is left, and its selectors and layout are unresolved (debt `:5`, `:13`).

## Batch proposal

**`feat(libSceVrTracker): time and GPU wait without a camera` (2: GetTime B-S, GpuWait C-real).**
- Semantics: shadPS4 a808e76 (`vr_tracker.cpp#L222-L232`, `#L244-L255`) and the done GpuWaitAndCpuProcess (`:124-126`).
- Tests: extend `guest_vr_tracker_devices_tests` (`GuestVrTrackerDevices.cpp`; `core/libs/CMakeLists.txt:494`, `:497`):
  - in `CheckBeforeInit` (`:97-104`): `sceVrTrackerGetTime(&t)` and `sceVrTrackerGpuWait(&wait)` give NotInit and leave `t` untouched;
  - after init (`CheckInit`, `:130-134`):
    - GetTime(nullptr) gives ArgumentInvalid;
    - two GetTime calls give non-decreasing values;
    - GpuWait(nullptr), and a 32-byte record with size 31, give ArgumentInvalid;
    - size 32 gives NotExecuteGpuSubmit;
  - after Term, both give NotInit again.
- Debt: move both names out of "Unknown function info" in `docs/dev/debt/libSceVrTracker.md` and say, as `:13` does, that they follow shadPS4 and are not measured on a PS5.
- Claim: #8650 (open) "implements" GetTime, GpuSubmit and GpuWait. Its head (2a28a06b9f) predates main's `sceVrTrackerInit`: it returns NOT_INIT unconditionally from all three. It no longer fits main and would need rebasing onto the initialized state.
- The three C-silent rows stay throws until a title needs them.

## Caveats

- Every answer here, done or proposed, is shadPS4's PS4 behaviour. The PS5 library may check more or answer differently (debt `:13`: "Not measured on a PS5").
- GetTime's clock base (process time in microseconds) is shadPS4's choice. Since GetResult takes a 64-bit time (`include/Api.hpp:50-60`, offset 16) and cannot be implemented, a wrong base is not observable today.
- GpuWait's size check (32) uses the only record size recovered (debt `:5`). Whether the PS5 accepts other sizes is unknown.
