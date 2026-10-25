# libSceIdu (8 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 8)

**What it is:** the in-store demo unit (IDU, kiosk) service: environment, staff mode, installing packages, checking installed apps and licences (RIF), rebooting.

**Existing state:**
- No CMakeLists. `Export.cpp` is built by `core/libs/CMakeLists.txt:159`.
- Nothing is done. All 8 rows are parameterless throws (`core/libs/prx/libSceIdu/Export.cpp:9-47`).
- Debt `docs/dev/debt/libSceIdu.md:5`: names from the PS5 3.20 inventory, no credible prototypes.
- No tests.
- Neighbours: the ShellCoreUtil IDU calls are all todo:
  - GetIDUMode `core/libs/prx/libSceSystemService/PendingExports.cpp:917`;
  - IsIDUState `:1137`;
  - SetIDUMode, ChangeToStaffModeForIDU and the IDU notifications.
- `libSceIduUtil` is a separate module (`core/libs/prx/libSceSysmodule/ModuleTable.hpp:221`).
- Orbital `db_types.json` at b321fcdbd7 has no `sceIdu` entry.

## Families

- **All 8 (D):**
  - no prototypes;
  - no done neighbour to say what a retail (non-IDU) console answers;
  - InstallPkg and Reboot are system actions the host must not perform;
  - IsAppInstalled and IsRifInstalled need argument forms that no source gives.

## Batch proposal

None.

## Caveats

- No claims.
- A "not an IDU" answer would come naturally from `sceShellCoreUtilGetIDUMode` or `IsIDUState`, if either is ever measured.
