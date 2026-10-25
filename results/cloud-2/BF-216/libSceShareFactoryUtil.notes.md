# libSceShareFactoryUtil (6 todo: D 6)

- **What it is:** a title's link to the Share Factory video editor (a system app). It launches Share Factory, returns from it to the game, and queries the launch status.
- **Existing state** (`core/libs/prx/libSceShareFactoryUtil/Export.cpp`):
  - 0 done. All six bodies (`:9` to `:34`) throw, and are declared without parameters (`include/Api.hpp:7-12`).
  - No state. No CMakeLists.txt; built by the generic `Export.cpp` rule (`core/libs/CMakeLists.txt:159`).
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 6.
- **D families:** every row is a parameterless 3.20 declaration with no credible prototype (`docs/dev/debt/libSceShareFactoryUtil.md:5`).
  - App switching (2): LaunchShareFactory, BackToGameFromShareFactory. No other app exists in AnyPS5, and the absent-app error is unknown. PM-088 left the same case D for `sceShellCoreUtilLaunchByUri`.
  - Status queries (2): GetLaunchStatus, GetLaunchStatusOfShareFactory. The output form and encoding are unknown.
  - Lifecycle (2): Initialize, Terminate. Their arguments are unknown and no title importer is recorded, so a silent stub has no title to justify it.
- **Batch proposal:** none (no A/B/C rows). No guest test executable exists.
- **Caveats:** if a title is found importing Initialize and Terminate with no arguments, they become C silent stubs returning 0, as `sceAppInstUtilInitialize`/`Terminate` were classified in BF-208.
