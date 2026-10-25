# libSceShellUIUtil (6 todo: C 2, D 4)

- **What it is:** a title's client of the PS5 ShellUI. It builds app URLs, launches URIs and tasks, and creates task tokens.
- **Existing state** (`core/libs/prx/libSceShellUIUtil/Export.cpp`):
  - 0 done. All six bodies (`:9` to `:34`) throw. No state; built by the generic `Export.cpp` rule (`core/libs/CMakeLists.txt:159`).
  - Commit bbadd9bda4 recovered the signatures of GetAppUrl (`:14`), LaunchByUri (`:24`) and LaunchTask (`:29`) from native wrappers. Per the debt (`docs/dev/debt/libSceShellUIUtil.md:5`), the wrappers confirm all the export signatures.
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 2, D 4.
- **C (2), silent stubs:** Initialize and Terminate return 0. No ShellUI service exists to connect to. This follows BF-208's `sceAppInstUtilInitialize`/`Terminate` and the done `sceRemoteplayInitialize`/`Terminate` (`libSceRemoteplay/Export.cpp:20`, `:25`). Later calls still throw. Each needs a title.
- **D (4):**
  - `GetAppUrl`: the URL comes from the ShellUI service, and the meaning of the input strings is unknown.
  - `LaunchByUri`: no other app or home screen exists, and part of the parameter is unknown.
  - `LaunchTask` and `CreateTaskToken`: ShellUI task IPC, with unknown token form, output and completion.
- **Batch proposal:** none (2 rows). No guest test executable exists.
- **Caveats:** no title importing libSceShellUIUtil is recorded in the tree. If one is found, the two silent stubs need a debt entry under Silent stubs naming it.
