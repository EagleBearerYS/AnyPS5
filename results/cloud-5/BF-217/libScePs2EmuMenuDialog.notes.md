# libScePs2EmuMenuDialog (7 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 7)

**What it is:** the in-game menu dialog of the PS2 emulator (PS2 Classics): Initialize, Open, UpdateStatus, GetStatus, GetResult, Close and Terminate. The names follow the common-dialog lifecycle.

**Existing state:**
- No CMakeLists. `Export.cpp` is built by `core/libs/CMakeLists.txt:159`.
- Nothing is done. All 7 rows are parameterless throws (`core/libs/prx/libScePs2EmuMenuDialog/Export.cpp:9-42`).
- Debt `docs/dev/debt/libScePs2EmuMenuDialog.md:5`: names from the PS5 3.20 inventory, no credible prototypes.
- No tests.
- Nearest pattern: libSceSocialScreenDialog's common-dialog state machine (`core/libs/prx/libSceSocialScreenDialog/Export.cpp:28-70`):
  - status NONE, INITIALIZED, RUNNING, FINISHED;
  - errors 0x80B80003/4/5/7/D (`:11-19`);
  - it reports "user cancelled";
  - its debt lists it as a silent stub with Orbital prototypes (`docs/dev/debt/libSceSocialScreenDialog.md:5`);
  - test `GuestSocialScreenDialog.cpp`, built at `libSceSocialScreenDialog/CMakeLists.txt:6`.
- Other PS2-emulator calls are todo: ShellCoreUtil and SystemService `*ForPs2Emu` in `libSceSystemService/PendingExports.cpp`.
- Orbital `db_types.json` at b321fcdbd7 has no `scePs2EmuMenuDialog` entry.

## Families

- **All 7 (D):** no prototype from the tree or a public source.
  - Treating this dialog as a common dialog is an inference from the names.
  - The Open parameter and the GetResult record (which menu item the user picked) are unknown.
  - Even with prototypes, the SocialScreenDialog pattern would give a C-silent "cancelled" dialog that needs a title.

## Batch proposal

None.

## Caveats

- PR 9634 (open) implements all 7. Under this guide it would need a prototype source and a title, and should record its result as a silent stub.
