# libSceRnpsAppMgr (27 todo: D 27)

**What it is:** a system-side application manager service (sysmodule id 0x80000076, `core/libs/prx/libSceSysmodule/ModuleTable.hpp:283`). It covers app install blocking, update checks and update/CRL URLs, URL/bundle requests, app info, ShellUI event exchange, app and UFS-image removal/recovery, and service start/stop.

**Existing state:**
- 0 done. Twenty-seven parameterless `NotImplemented_nid_no_patch` declarations: `core/libs/prx/libSceRnpsAppMgr/Export.cpp:9-142`, one every five lines from `:9` to `:139`.
- No state, no `CMakeLists.txt` (built from the glob at `core/libs/CMakeLists.txt:146`, `:159`) and no guest test.
- Debt `docs/dev/debt/libSceRnpsAppMgr.md:5`: the names and NIDs come from the PS5 3.20 export inventory, no credible prototypes were found, and the declarations are not an ABI claim.
- Nothing else in the tree references the library, apart from the sysmodule table.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 27.

## Families

All D: unknown signatures for a system service that the tree does not model.
- **Install blocking (2):** `BlockAppInstall` and `UnblockAppInstall`.
- **Updates (6):** `CreateUpdateRequest`, `DestroyUpdateRequest`, `GetUpdateProgress`, `RequestUpdateCheck`, `SetUpdateUrl` and `SetCrlUrl`.
- **URL/bundle requests (3):** `CreateUrlRequest`, `FinishUrlRequest` and `DestroyBundle`.
- **App info (3):** `GetAppInfo`, `GetAppInfoDebugString` and `GetAppInfoList`. The record layouts are unknown.
- **ShellUI events (6):** `RegisterShellUI`, `UnregisterShellUI`, `PushEventForShellUI`, `GetEventForShellUI`, `HasEventForShellUI` and `SetEventFlagForClients`.
- **Removal and recovery (4):** `RemoveApp`, `RemoveAppOnSystemShutdown`, `RemoveUfsImageOnSystemShutdown` and `RecoverUfsImage`.
- **Lifecycle (3):** `Initialize`, `Terminate` and `StartService`.

## Batch proposals

- None: there are no A, B-S, B-M or C-real rows.

## Dependency notes

- Prototypes must be recovered first. Most of these calls belong to system processes (ShellUI, the installer), not games, so a title import is unlikely to justify the work.
