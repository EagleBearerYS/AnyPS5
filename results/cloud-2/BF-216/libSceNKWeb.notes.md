# libSceNKWeb (2 todo: C 2)

- **What it is:** initialization and termination of the PS5 NK web engine, whose API is libSceNKWebKit.
- **Existing state** (`core/libs/prx/libSceNKWeb/Export.cpp`):
  - 0 done. Initialize (`:9`) and Terminate (`:14`) throw.
  - The debt (`docs/dev/debt/libSceNKWeb.md:5`) confirms both take no arguments and return a 32-bit status.
  - libSceNKWebKit's 150 exports are all throwing declarations (`docs/dev/debt/libSceNKWebKit.md:5`).
  - Built by the generic `Export.cpp` rule (`core/libs/CMakeLists.txt:159`).
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 2 (silent stubs), D 0.
- **C (2), silent stubs:** both return 0. No web engine exists to start or stop, so success only lets a caller continue; any NKWebKit call still throws. This follows BF-208's `sceAppInstUtilInitialize`/`Terminate` and the done `sceRemoteplayInitialize`/`Terminate`.
- **Batch proposal:** none (2 rows). No guest test executable exists.
- **Caveats:**
  - No title importing libSceNKWeb is recorded. CONTRIBUTING requires one, and a debt entry under Silent stubs.
  - A real NK web engine would be B-L (an embedded browser engine, as a new submodule). It belongs to libSceNKWebKit, not to these two rows.
  - Whether the console refuses a second Initialize is unknown, so no initialized state is proposed.
