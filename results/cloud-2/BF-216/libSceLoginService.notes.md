# libSceLoginService (1 todo: D 1)

- **What it is:** a client of the PS5 login service, which lets a title request device or controller sign-in.
- **Existing state** (`core/libs/prx/libSceLoginService/Export.cpp`):
  - 2 done. Initialize (`:8`) and Terminate (`:12`) return 0 and keep no state. They are silent stubs for EA SPORTS FC 27 (PPSA34015) (`docs/dev/debt/libSceLoginService.md:5`).
  - Test: `guest_login_service_tests` (`core/libs/tests/GuestLoginService.cpp`), built at `libSceLoginService/CMakeLists.txt:6` and registered at `:10`.
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 1.
- **D:** `sceLoginServiceRequestDevices` (`:16`). The native wrapper takes one read-only request pointer and sends exactly 64 bytes to the login service (debt `:9`).
  - Known: null returns 0x813a0003 before the init check; an uninitialized service returns 0x813a0001.
  - Unknown: the request fields, and the service's answer for a valid request when no login service exists.
- **Batch proposal:** none.
- **Caveats:**
  - Giving the 0x813a0001 answer would need an initialized flag in Initialize/Terminate, which the silent stubs do not keep. Whether the console refuses a second Initialize is also unknown.
  - The row stays D until the absent-service answer is measured.
