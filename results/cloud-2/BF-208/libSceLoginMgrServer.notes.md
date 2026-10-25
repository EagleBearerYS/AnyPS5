# libSceLoginMgrServer (30 todo: D 30)

**What it is:** the server side of the system login manager (sysmodule 0x80000045, `core/libs/prx/libSceSysmodule/ModuleTable.hpp:241`). ShellCore uses it to back user creation and login, the login common dialog, libSceLoginService requests, easy sign-in, Share Play pad handover and PS VR (Morpheus) user assignment. Its clients in the tree are libSceLoginService and libSceLoginDialog, which are silent stubs with no service behind them (their debt files).

**Existing state:**
- 0 done.
- Every export in `core/libs/prx/libSceLoginMgrServer/Export.cpp` is a parameterless placeholder that calls `NotImplemented`. The debt (`docs/dev/debt/libSceLoginMgrServer.md:5`) says the names and NIDs come from the PS5 3.20 export inventory, that no credible prototypes exist, and that the empty parameter lists are not an ABI claim.
- No `CMakeLists.txt`: the library is built by the generic rule (`core/libs/CMakeLists.txt:158`). No test executable exists.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 30.

**D families** (one basis: no public prototype, and the work is private ShellCore IPC):
- user management (7): ConvertToUserId, CreateUser, DestroyUser, GetLogoutInfo, SetLoginFlag, SetUserStatus, UpdateUserIdRalatedToPadUniqueId;
- login common-dialog server (6): Dialog*, IsRequestedCdlgClose;
- PS VR assignment (5): Morpheus*, NotifyHmdConnectInfo;
- Share Play (3), lifecycle and shell state (3), safety notice (2), LoginService requests (2), easy sign-in (2).

**Batch proposal:** none.

**Caveat:** a game process should not import this module. If a title does, its call sites are the only source of prototypes.
