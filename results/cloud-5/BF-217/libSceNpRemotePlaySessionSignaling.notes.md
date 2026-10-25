# libSceNpRemotePlaySessionSignaling (10 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 10)

**What it is:** PSN signaling for Remote Play sessions: init and terminate, create, join and leave a session, connection status, local network info, custom properties and memory info.

**Existing state:**
- No own `CMakeLists.txt`. `core/libs/prx/libSceNpRemotePlaySessionSignaling/Export.cpp` is built by `core/libs/CMakeLists.txt:159`, so the rows are exported.
- 0 done. No state and no error constants.
- The sysmodule table names it as internal id 0x8000009a (`core/libs/prx/libSceSysmodule/ModuleTable.hpp:317`).
- Debt: `docs/dev/debt/libSceNpRemotePlaySessionSignaling.md:5` says no credible prototypes were found and that callers must not call these until the signatures are verified.
- No tests.
- Neighbour: libSceNpSessionSignaling, with 18 done and 8 todo. Four of its done names match these: Initialize (`core/libs/prx/libSceNpSessionSignaling/Export.cpp:88`), Terminate (`:158`), GetConnectionStatus (`:166`) and GetLocalNetInfo (`:177`).
  - Its offline model: sessions never activate, and lookups end at not-found or unavailable (`docs/dev/debt/libSceNpSessionSignaling.md:7`, `:12`).
  - Its GetCustomProperty, SetCustomProperty and GetMemoryInfo are todo too.

## Families

- **D (10), all with no prototype:**
  - Lifecycle (Initialize, Terminate): the init layout and error range are unknown. NpSessionSignaling's 40-byte init parameter (`docs/dev/debt/libSceNpSessionSignaling.md:5`) is not shown to be shared.
  - Sessions (CreateJoinSession, JoinSession, LeaveSession): session ids and parameters are unknown.
  - Queries (GetConnectionStatus, GetLocalNetInfo, GetCustomProperty, GetMemoryInfo) and SetCustomProperty: the layouts are unknown.

## Batch proposal

None.

## Caveats

- Lead: if a title shows that the Remote Play forms take NpSessionSignaling's argument lists, the four overlapping names could be answered as in `libSceNpSessionSignaling/Export.cpp:88-183`. The error range (0x8055xxxx there) would still need confirming for this module.
