# libSceScreenShot (6 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 6)

**What it is:** screenshot control: enabling or disabling capture, capture notifications, overlay images, and the capture and parameter calls.

**Existing state** (`core/libs/prx/libSceScreenShot/Export.cpp`; built by `CMakeLists.txt:1`):
- 7 done:
  - Disable and Enable set and clear one process-wide flag (`:22`, `:63-71`);
  - IsDisabled reports it, and refuses id -1 or a null output (`:73-77`);
  - Enable/DisableNotification return 0 as silent stubs (`:79-85`; debt `:5`);
  - SetOverlayImage and SetOverlayImageWithOrigin check the arguments and the file as the PS5 module does, then keep nothing (`:31-61`; debt `:6`).
- Errors: 0x80BE0001, 0x80BE0003 and 0x80BE000E (`:14-16`).
- The overlay limits depend on the SDK version (`:17`, `:24-35`).
- Debt (`docs/dev/debt/libSceScreenShot.md:11`): the six todo names are in the 3.20 export inventory, and "no credible prototypes were found".
- Tests:
  - `guest_screenshot` and `guest_screenshot_large_overlay` (`core/libs/tests/GuestScreenShot.cpp`): executable at `CMakeLists.txt:6`, registered at `:10-11`.
  - Neither calls a todo row.

## Families

- **Parameters (4): D.** GetAppInfo, GetDrcParam, SetDrcParam and SetParam. No prototype exists, and the records are unknown.
- **Capture (1): D.** No prototype, and the project takes no screenshots (debt `:6`).
- **IsVshScreenCaptureDisabled (1): D.**
  - It is not shown to share IsDisabled's `(id, int*)` arguments (`:73`).
  - Whether the system-software capture state is the same flag is unknown.

## Batch proposal

None. No claims.

## Caveats

- Once a prototype is known, SetParam and SetDrcParam would be C-silent at best, since no screenshot is ever taken. They would then be written only for a title that needs them.
