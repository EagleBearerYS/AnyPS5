# libSceUrlConfigResolver (8 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 8)

**What it is:** a system-side resolver of URL and web-app configuration. It hands out an application config, a default query parameter, a device id, a JavaScriptCore heap soft limit, release and system versions, and an update check and request. It is not in the sysmodule table (`core/libs/prx/libSceSysmodule/ModuleTable.hpp` has no entry for it).

**Existing state:**
- Built: no `CMakeLists.txt` of its own, so `core/libs/CMakeLists.txt:159` compiles `core/libs/prx/libSceUrlConfigResolver/Export.cpp`.
- 0 done. Eight parameterless declarations (`Export.cpp:9`, `:14`, `:19`, `:24`, `:29`, `:34`, `:39`, `:44`). There is no state, no error constant and no test.
- Debt (`docs/dev/debt/libSceUrlConfigResolver.md`):
  - `:5`: GetReleaseVersion, GetSysVer, IsUpdated and Update have unverified signatures and behaviour;
  - `:6`: GetAppConfig, GetDefaultQueryParameter, GetDeviceId and GetJscHeapSizeSoftLimit are in the 3.20 inventory with "no credible prototypes".

## Families

- **D (8): all rows.**
  - No prototype.
  - The answers are system configuration whose formats are not in the tree or a cited source: the app config record, the query string, the device-id format, the heap limit, the version encodings, and the update state and request.
  - No other library has these names. The other `GetDeviceId` exports on main (pad, move, keyboard, mouse, HMD2 and ShellCoreUtil, all todo) are device ids of a different kind, not twins.

## Batch proposal

None.

## Caveats

- GetSysVer might report the same system software version the kernel reports elsewhere. With no prototype and no stated encoding, that link is only a lead.
