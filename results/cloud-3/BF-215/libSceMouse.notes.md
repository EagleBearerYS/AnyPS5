# libSceMouse (10 todo: D 10)

**What it is:** USB mouse input: Init, Open, Read, Close and absolute-position reset for titles. It also has system-side device-bus calls (port connect and disconnect, device open and info, `MbusInit`, process privilege) and user settings (hand type, pointer speed).

**Existing state:**
- 5 done: `Init`, `Open`, `Close`, `Read` and `ResetAbsolutePosition` (`core/libs/prx/libSceMouse/Export.cpp:7-29`). Each one calls the `Mouse::` namespace in `src/mouse_impl.cpp` (declared in `include/MouseState.hpp:18-26`).
- Host input arrives through `MousePublishInput_nid_no_patch` (`Export.cpp:23`) from libSceVideoOut's `MouseInput.cpp`.
- Error constants are at `include/mouse_structs.h:14-17`: `INVALID_ARG` 0x80DF0001, `INVALID_HANDLE` 0x80DF0003, `ALREADY_OPENED` 0x80DF0004 and `NOT_INITIALIZED` 0x80DF0005.
- Test: `mouse_tests` (`core/libs/prx/libSceMouse/tests/Mouse.cpp`), registered at `core/libs/prx/libSceMouse/CMakeLists.txt:10` and `:17`. libSceVideoOut also registers `video_out_mouse_tests` (`core/libs/prx/libSceVideoOut/CMakeLists.txt:36`, `:43`).
- Debt `docs/dev/debt/libSceMouse.md:5`: the 10 todo prototypes copy shadPS4's PS4 no-argument stubs (commit f68923fe) "solely to resolve known imports", with no ABI claim.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 10.

## Families

- **D (6), device-bus management:** `ConnectPort`, `DisconnectPort`, `DisconnectDevice`, `DeviceOpen`, `MbusInit` and `SetProcessPrivilege`.
  - These are unknown signatures for system-side calls.
  - The device bus they manage is a service the tree does not model.
- **D (2), device queries:** `GetDeviceInfo` and `DebugGetDeviceId`. Their output layouts are unknown.
- **D (2), user settings:** `SetHandType` and `SetPointerSpeed`.
  - Their signatures are unknown.
  - Whether the console applies them to what `sceMouseRead` returns (swapped buttons, scaled deltas) is not known.
  - So they cannot be called setters "of something never used", and they are not C-silent candidates.

## Batch proposals

None. No row is A, B or C-real.

## Caveats

- If a title calls `SetHandType` or `SetPointerSpeed` and its call site gives the arguments, they could become C-silent rows, but only after a console check that `sceMouseRead`'s data is unaffected.
