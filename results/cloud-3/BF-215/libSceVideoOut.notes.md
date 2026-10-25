# libSceVideoOut (7 todo: D 7)

**What it is:** display output: ports and buses, buffer registration, CPU, command-buffer and EOP flips, vblank and flip events, output modes, HDR, color-space conversion, VRR, plus a hardware cursor and flip-master pairing between ports.

**Existing state:**
- 55 done.
- Main sources are in `core/libs/prx/libSceVideoOut/src/`:
  - `Output.cpp`: open, configure and status calls;
  - `Buffer.cpp`: buffer registration and labels;
  - `Event.cpp`: event queues;
  - `VideoOutDriver.cpp`: present threads and flips.
- The todo rows are in `core/libs/prx/libSceVideoOut/Unimplemented.cpp:11-39` and `:66`.
- Debt `docs/dev/debt/libSceVideoOut.md`:
  - `:14`: cursor prototypes come from Prospero Win; the image format and the return values are unknown;
  - `:33`: the HMD bus needs a VR backend;
  - `:34`: overlay buses are not composited;
  - `:40`: `sceDbgVideoOutAddOutputModeVr60Privilege` is a parameterless pending declaration.
- Tests: about 20 registered in `core/libs/prx/libSceVideoOut/CMakeLists.txt`. Among them:
  - `guest_video_out_cursor_tests` (`core/libs/tests/GuestVideoOutCursor.cpp`), at `:77` and `:81`. It asserts that the four cursor calls throw "not implemented".
  - `video_out_flip_tests` (`:50`, with flip-mode tests from `:96`).

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 7.

## Families

- **D (4), hardware cursor:** `CursorEnable`, `CursorDisable`, `CursorSetImageAddress` and `CursorSetPosition`.
  - The parameter lists are known (debt `:14`).
  - The console's return values, the cursor image's format and size, and its coordinate space are not.
  - A cursor is visible output, so it cannot be a silent stub.
- **D (2), flip master:** `SetFlipMaster` and `ClearFlipMaster`.
  - The `(handle, masterHandle)` and `(handle)` declarations cite no source, and no debt line covers them.
  - Pairing one port's flips to another is not modelled (debt `:34`). The console's answer is unknown.
- **D (1), `sceDbgVideoOutAddOutputModeVr60Privilege`:**
  - Its signature is unknown (debt `:40`).
  - VR output needs a VR backend (debt `:33`).

## Batch proposals

None. No row is A, B or C-real.

## Caveats

- The flip-master declarations have typed parameters but no cited source. A debt entry for them is missing; the debt file should name where `(handle, masterHandle)` came from.
