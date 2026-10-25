# libSceBluetoothHid (11 todo: B-S 2, D 9)

**What it is:** the Bluetooth HID host interface: init, a callback for device events, registration by vendor/product, and per-device report I/O (input, output, feature, interrupt, descriptor, name, info, disconnect).

**Existing state:**
- 6 done, in `core/libs/prx/libSceBluetoothHid/Export.cpp`.
- State: `stateMutex`, `initialized` and `callbackRegistered` (`:19-21`).
- Errors: `InvalidArgument` 0x81540001, `NotInitialized` 0x81540004 and `NoCallback` 0x81540006 (`:22-24`).
- Recovered layouts are in `include/Api.hpp`, with size asserts at `Export.cpp:10-16`:
  - `BluetoothHidThreadParam` (`:9-15`, 24 bytes);
  - `BluetoothHidParam` (`:17-21`, 16 bytes);
  - `BluetoothHidEvent` (`:23-33`, 104 bytes).
- `BluetoothHidDeviceInfo` and `BluetoothHidDeviceName` are incomplete types (`:35`, `:37`).
- `sceBluetoothHidRegisterCallback` (`Export.cpp:35-49`) checks `param->reserved` and `threadParam->reserved`. It throws for a custom worker thread (`:43`).
- There is no device backend or event worker, so callbacks never fire (`docs/dev/debt/libSceBluetoothHid.md:5`). Output layouts of name and info are unresolved (debt `:9`).
- Tests: `guest_bluetooth_hid_tests` (`core/libs/tests/GuestBluetoothHid.cpp`), registered at `libSceBluetoothHid/CMakeLists.txt:6` and `:10`.

**Counts:** A 0, B-S 2, B-M 0, B-L 0, C-real 0, C-silent 0, D 9.

## Families

- **B-S (2): parameter initializers over the recovered layouts.**
  - `sceBluetoothHidThreadParamInitialize(param, priority, affinityMask)` writes `priority`, `affinityMask` and `reserved = 0` into `BluetoothHidThreadParam`.
  - `sceBluetoothHidParamInitialize(param, threadParam)` writes `threadParam` and `reserved = 0` into `BluetoothHidParam`.
  - These are the fields `RegisterCallback` validates (`Export.cpp:39-44`). The basis is in-tree only: no public source documents the initializers.
- **D (9): no device backend.**
  - `GetInputReport`, `SetOutputReport`, `GetFeatureReport`, `SetFeatureReport`, `InterruptOutput`, `GetReportDescriptor` and `DisconnectDevice` act on a connected device that the host never has. The tree has no console error for an unknown handle or id.
  - `GetDeviceName` and `GetDeviceInfo` also have unresolved output layouts (debt `:9`).

## Batch proposals

1. **"bluetoothhid: parameter initializers" (2):** `sceBluetoothHidParamInitialize` (`Export.cpp:120`) and `sceBluetoothHidThreadParamInitialize` (`:130`).
   - Files: `core/libs/prx/libSceBluetoothHid/Export.cpp` and `docs/dev/debt/libSceBluetoothHid.md`.
   - Semantics: the recovered layouts (`include/Api.hpp:9-21`) and the checks `RegisterCallback` applies to them (`Export.cpp:39-44`). Throw for a null `param`, and for priority or affinity values whose console handling is unmeasured if any range check is wanted.
   - Tests: extend `guest_bluetooth_hid_tests` (`GuestBluetoothHid.cpp`, `CMakeLists.txt:6`):
     - ThreadParamInitialize(&tp, 700, 0x1fff) yields priority 700, mask 0x1fff and reserved 0, even after the record was pre-filled with 0xAA;
     - ParamInitialize(&p, nullptr) yields threadParam null and reserved 0, and RegisterCallback(&Callback, nullptr, &p) returns 0;
     - ParamInitialize(&p, &tp) stores `&tp`, and RegisterCallback then reaches the custom-worker throw;
     - null `param` throws.
   - The batch is small and could merge with `libSceContentExport`'s `sceContentExportInit` batch (also B-S, 1 row) as one "parameter initializers" task.

## Dependency notes

- The D rows need a new subsystem: a host HID backend (for example through the tree's existing `3rdparty/libusb-cmake` or SDL2) plus the event worker. The 104-byte callback event layout is already recovered (`include/Api.hpp:23-33`), but the console errors for absent devices and the name/info layouts still need measuring.
