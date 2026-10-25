# libSceUpdateService (BF-208 classification)

- **What it is:** the system software update service client (`sceUpsrv*`): update checks, update tasks from PSN, disc or PUP files, the system_ex partition banks, EULA and feature-list strings, and the update session.
- **Existing state:** 0 done, 40 todo.
  - `core/libs/prx/libSceUpdateService/Export.cpp:9-207` holds 40 parameterless `NotImplemented_nid_no_patch` declarations.
  - There is no state, no CMakeLists.txt and no guest test.
- **Debt:** `docs/dev/debt/libSceUpdateService.md:5` (3.20 export inventory, no credible prototypes).
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 40.
- **D families (private system-service IPC, no prototypes):**
  - update tasks (13);
  - update checks (8);
  - EULA and URL strings (8);
  - session and lifecycle (6);
  - system_ex bank update and switch (5).
  - A PC has no system software, PUP or partition banks to update.
  - A C answer ("no update available" or a permission error for a game process) would need a measured console value, and the tree has none.
- **Batch proposals:** none. No guest test executable exists.
- **Caveats:** game titles normally check for title patches through libSceGameUpdate, which is done (7/7). This library is system-side.
