# libSceImeDialog (1 todo: D 1)

**What it is:** the system on-screen keyboard dialog. Here, text comes from the host keyboard through libSceIme while the dialog is running.

**Existing state:**
- 13 done, in `core/libs/prx/libSceImeDialog/Export.cpp`.
- State: `g_mutex` and `g_dialog` (`:38-39`), with statuses none/running/finished and end statuses ok/canceled/aborted (`:13-18`).
- Errors (`:20-27`): `Busy` 0x80bc0001, `Suspending` 0x80bc0009, `NotOpened` 0x80bc0002, `NotInUse` 0x80bc0107, max-length 0x80bc0016, null buffer 0x80bc0017, null param 0x80bc0031 and `Internal` 0x80bc00ff. They follow shadPS4 and SharpProspero at the commits the debt cites (`docs/dev/debt/libSceImeDialog.md:10-12`).
- `sceImeDialogInitInternal` forwards to `sceImeDialogInit` (`:202-204`).
- Tests: `guest_ime_dialog_tests` (`core/libs/tests/GuestImeDialog.cpp`), registered at `libSceImeDialog/CMakeLists.txt:7` and `:12`.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 1.

## Families

- **D (1):** `sceImeDialogInitInternalWithSubInfo` (`:211`) is a parameterless pending declaration whose signature and behaviour are unverified (debt `:14`). Neither shadPS4 nor SharpProspero, as cited, gives the sub-info argument, so forwarding to `InitInternal` would be a guess.

## Batch proposals

- None: there are no A, B-S, B-M or C-real rows. Once a prototype exists (`param`, `extended` and a sub-info record), it is likely a B-S forward to `sceImeDialogInit`, like `InitInternal` (`:202-204`).
