# libSceMoveTracker (16 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 16)

**What it is:** PlayStation Move tracking through the camera. It covers initialization on GPU queues, per-frame camera and controller updates, tracked-state queries, yaw calibration, and capture, playback and recording helpers. Its module id is 0xb1 (`core/libs/prx/libSceSysmodule/ModuleTable.hpp:67`).

**Existing state** (`core/libs/prx/libSceMoveTracker/Export.cpp`; there is no CMakeLists, so `core/libs/CMakeLists.txt:159` builds `Export.cpp`):
- 2 done, neither with a source or a debt entry:
  - `sceMoveTrackerGetVersion` returns 2 (`:44-46`);
  - `sceMoveTrackerRecordStop` returns 0 (`:88-90`).
- No state and no error constants. The records are incomplete types (`include/Api.hpp:5-11`).
- Debt (`docs/dev/debt/libSceMoveTracker.md:5`):
  - recovered signatures for calibration direction, camera and controller input, capture init, the state query, the memory-size query and playback output;
  - record sizes: camera and controller 24 bytes, state 160, playback metadata 16; their fields are not reconstructed;
  - Init's checks: a work-memory pointer, a nonzero 64-bit argument of unresolved type, and two GPU queue selectors below eight;
  - the capture and playback helper signatures still need checking.
- No tests.
- Neighbour: libSceMove reports only disconnected controllers, with its own range 0x80EE0001-0x80EE0007 (`core/libs/prx/libSceMove/Export.cpp:14-18`; `docs/dev/debt/libSceMove.md:5`). That range is not MoveTracker's, and no MoveTracker error is in the tree.

## Families

- **Lifecycle and sizing (3): D.**
  - Init: its checks are known, but their error codes and the answer without a camera are not.
  - Term: its answer without Init is unknown.
  - GetWorkingMemorySize: the two sizes are console constants that are not recorded.
- **Tracking (5): D.**
  - CameraUpdate, ControllersUpdate, GetState, CalibrateYaw and CalibrateReset.
  - The records are unreconstructed. Even with known semantics this would need a camera tracker, which is a B-L subsystem.
- **Capture, playback and recording (8): D.**
  - CaptureInit, CaptureTerm, PlayGetImages, PlayGetStates, PlayStart, PlayStep, RecordSave and RecordStart.
  - The helper signatures are unverified, and the recording format is not public.

## Batch proposal

None. No claims touch this library.

## Caveats

- **Unsourced constants in done functions.** GetVersion's 2 and RecordStop's 0 have no debt entry or source. Under CONTRIBUTING, RecordStop is a silent stub that needs a title and a debt line.
- **First row to unblock.** A measured no-camera answer for Init would let the lifecycle rows become C-real.
