# libSceHubAppUtil (3 todo: D 3)

- **What it is:** deep links from a title into PS5 system hub views (live-event detail, PlayStation subscriptions), plus an installation-status query.
- **Existing state** (`core/libs/prx/libSceHubAppUtil/Export.cpp`):
  - 0 done. The three bodies (`:9`, `:14`, `:19`) throw and are declared without parameters (`include/Api.hpp:7-9`).
  - No state. Built by the generic `Export.cpp` rule (`core/libs/CMakeLists.txt:159`).
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 3.
- **D:** all three are parameterless 3.20 declarations with no credible prototype (`docs/dev/debt/libSceHubAppUtil.md:5`).
  - The two deep links target system UI that does not exist in AnyPS5. Their arguments and the absent-app error are unknown, as for `sceShellCoreUtilLaunchByUri` in PM-088.
  - GetInstallationStatus has an unknown subject and output form.
- **Batch proposal:** none. No guest test executable exists.
- **Caveats:** none.
