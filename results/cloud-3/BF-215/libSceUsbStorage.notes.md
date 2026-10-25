# libSceUsbStorage (12 todo: C-silent 6, D 6)

**What it is:** USB mass-storage access for titles:
- device list and info;
- map and unmap a directory on a device, with the mount point;
- directory listing through getdents;
- event callbacks for attach and detach and for map availability;
- debug fake write-protect and map-lock switches.

**Existing state:**
- 9 done, all in `core/libs/prx/libSceUsbStorage/Export.cpp`. AnyPS5 has no USB mass-storage passthrough, so no device is ever attached (debt `docs/dev/debt/libSceUsbStorage.md:17`):
  - `Init` and `Term` keep no state (`:11-18`; silent stubs per debt `:5`);
  - the device list is empty (`:89-95`);
  - `IsExist` reports absence (`:104-109`);
  - `GetDeviceInfo`, `RequestMap`, `RequestMapWSB` and `GetdentsOpen` answer DEVICE_NOT_FOUND 0x80f40004 (`:81-87`, `:111-120`, `:160-169`, `:131-137`);
  - `GetdentsRead` answers 0x80f40005 with zero bytes (`:139-147`).
- Error constants and callback event range: `core/libs/prx/libSceUsbStorage/UsbStorage.hpp:11-16` (InvalidArgument 0x80f40002, DeviceNotFound 0x80f40004, NotSupported 0x80f40005, event types 1 and 2). Prototypes are at `:25-40`.
- Argument validation of the register, unregister and unmap family follows shadPS4 at `f68923f` (debt `:16`).
- Tests: `guest_usb_storage_tests` (`core/libs/tests/GuestUsbStorage.cpp`, which already checks that the six callback calls throw past validation, `:35-70` and `:72-109`), registered at `core/libs/prx/libSceUsbStorage/CMakeLists.txt:7`, with `add_test` at `:11`.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 6, D 6.

## Families

- **C-silent (6), callback registration (partial):** the register and unregister calls for device events (`:20`, `:32`), for map-available (`:43`, `:53`) and for map-unavailable (`:62`, `:72`).
  - Validation is done, and only the success path still throws.
  - A registered callback can never fire, because no device is ever attached (debt `:17`). shadPS4 returns 0 without storing anything (debt `:9`).
  - Unregister needs care: the console's answer for a callback that was never registered is unknown (debt `:9`). A silent stub must therefore keep the registrations it accepted, succeed only for those, and keep the marker otherwise.
  - Silent stub, needs a title.
- **D (6):**
  - `RequestUnmap` (partial: the null directory is refused at `:122-126`) and `GetdentsClose` (`:149`): the console's answer for a never-mapped directory and a never-opened descriptor is unknown (debt `:10`). The other no-device libraries throw on that path too.
  - `GetMountPointOfShellCore` (`:97`): the prototype is known, but not what a console with no mapped device writes or returns (debt `:12`).
  - `ClearFakeWriteProtection`, `SetFakeMapLockForDebug` and `SetFakeWriteProtection` (`:155`, `:171`, `:176`): unknown signatures (debt `:11`).

## Batch proposals

None for A, B or C-real rows.

If a title needs the callbacks, there is a conditional task: **"usb-storage: accept callback registration with no device" (6, C-silent).**
- Files:
  - `core/libs/prx/libSceUsbStorage/Export.cpp`: a mutex-guarded set of `(kind, eventType or device and directory, callback)` registrations;
  - `docs/dev/debt/libSceUsbStorage.md`: move the six from "Unknown function info" to "Silent stubs", naming the title.
- Tests, in `guest_usb_storage_tests`, replacing the "throws past validation" checks in `TestRegisterCallbacks` and `TestMapCallbacks` (`GuestUsbStorage.cpp:35`, `:72`):
  - register, then unregister, returns 0;
  - unregistering a callback that was never registered still throws;
  - the validation answers are unchanged.

## Dependency notes

- A real passthrough would be a new subsystem. It would build on `3rdparty/libusb-cmake` (already a submodule) or on host mount points, and debt `:17` and `:18` describe the device model it should follow. That passthrough is what would make the callbacks observable and give `RequestUnmap`, `GetdentsClose` and `GetMountPointOfShellCore` real answers.
