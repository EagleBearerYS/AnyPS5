# libSceMove (11 todo: A 0, B-S 0, B-M 0, B-L 0, C 0, D 11)

Upstream main 2742004ed2.

- **What it is:** the PlayStation Move motion-controller API: init/term, open/close per user and slot, device and extension-port info, state reads, vibration and light sphere, plus calibration, device-id, Bluetooth-address and tracker variants.
- **Existing state:** 10 done.
  - Error constants and handle numbering at `core/libs/prx/libSceMove/Export.cpp:12-20`; state `stateMutex`, `initialized`, `openHandles` (`:22-30`); `checkHandle` (`:32`) does init, then argument, then handle validation.
  - The host only models disconnected controllers: every query and setter returns 1 (`MOVE_NO_CONTROLLER_CONNECTED`) without writing output (`:78-110`), and `sceMoveResetLightSphere` returns 0 after the checks (`:112`). Debt `docs/dev/debt/libSceMove.md:5` records this order and answer as confirmed by the native module.
  - `sceMoveOpen` (`:59`) hands out monotonically increasing handles from 0x30B0100 and returns 0x80EE0007 for a duplicate slot (debt `:6`).
  - Test: `guest_move_tests` (`core/libs/tests/GuestMove.cpp`), built at `core/libs/CMakeLists.txt:504` and registered at `:508` (`add_test(NAME guest_move COMMAND guest_move_tests)`).
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 11.
- **B and C families:** none.
  - The disconnected answer is known and would make these C "real behaviour" rows, but it is an answer about arguments: the init check, the null-argument check and the handle check all read them, and a C row may not write or skip outputs it cannot name.
  - All 10 todo declarations other than `sceMoveOpen` are `(void)` in `include/Api.hpp:18-27`, with no debt entry and no public prototype in the tree. They stay D until their argument lists are recorded.
- **D families (11):**
  - Queries (5): GetBluetoothAddress, GetCalibrationInfo, GetDeviceId, IsReproductionModel, IsValidHandle. Prototypes unknown; IsValidHandle and IsReproductionModel also have no known return convention.
  - Setters and writers (4): SetCalibrationMode, SetExtensionPortOutput, SetLightSphereForTracker, WriteCalibrationInfo. Prototypes and record layouts unknown.
  - Tracker read (1): ReadStateRecentForTracker. Record and arguments unknown.
  - `sceMoveOpen`, partial: only a userId other than the initial user (0x10000000) throws (`:62`). The native open resolves the user through the input service (debt `:6`); its answer for a user who is not logged in is not recorded. The 8-slot limit with 0x80EE0005 is known but not modelled; it does not throw.
- **Batch proposal:** none, because there is no A, B or C family.
  - Once prototypes are recorded, most of the 10 rows can move together to C "real behaviour" with the existing `checkHandle` order (IsValidHandle would instead read `openHandles`), as `feat(libSceMove): disconnected answers for the remaining queries`, extending `RequireNotInit` and `RequireUnknownHandle` in `GuestMove.cpp:47` and `:64` with a buffer that stays untouched.
- **Caveats:**
  - `IsValidHandle` is the likeliest to be cheap once its signature is known: it reads `openHandles` and needs no controller.
  - The Open gap is a user-model question (one user on this PC), not a controller question.
