# libSceSocialScreen (11 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 11)

**What it is:** PS VR social screen: the "separate mode" that shows the TV a different picture from the headset, its parameters and ignore flag, high-frame-rate capture reservation, and a debug daemon crash.

**Existing state:**
- No CMakeLists. `Export.cpp` is built by `core/libs/CMakeLists.txt:159`.
- Nothing is done. All 11 rows are parameterless throws (`core/libs/prx/libSceSocialScreen/Export.cpp:9-62`).
- Debt `docs/dev/debt/libSceSocialScreen.md:5`: names from the PS5 3.20 inventory, "no credible prototypes".
- No tests. Module ids in `core/libs/prx/libSceSysmodule/ModuleTable.hpp:128`, `:335`.
- Public source checked: Orbital `db_types.json` at b321fcdbd7 gives seven prototypes:
  - CloseSeparateMode `()` `#L2169`;
  - ConfigureSeparateMode `(const struct*)` `#L2170`;
  - Initialize `()` `#L2178`;
  - InitializeSeparateModeParameter `(struct*)` `#L2179`;
  - OpenSeparateMode `()` `#L2180`;
  - SetMode `(enum mode)` `#L2181`;
  - Terminate `()` `#L2182`.

  The tree already cites this Orbital commit for libSceSocialScreenDialog. So the debt is stale for those seven names.
- Neighbours are todo: the libSceHmd social-screen calls (`core/libs/prx/libSceHmd/Export.cpp:145` and others), `sceShellCoreUtilGet/SetSocialScreenStatus` (`core/libs/prx/libSceSystemService/PendingExports.cpp:1012`, `:1462`) and `sceMbusSetSocialScreenAudioMode`. libSceSocialScreenDialog is done, but it is a different library and a silent stub.

## Families

- **With an Orbital prototype (7, D):** prototypes are known, answers are not.
  - What a console without a PS VR headset answers (success, or an error) is unmeasured.
  - No social-screen error constant exists in the tree.
  - The parameter record and mode values are unknown.
- **Without a prototype (4, D):** CrashDaemon (debug), Get/SetIgnoreSeparateMode, ReserveHighFrameRateVideoCaptureRequest.

## Batch proposal

None.

## Caveats

- No claims.
- Updating debt `:5` with the Orbital prototypes would be a debt-only change.
- A console measurement without PS VR (Initialize, OpenSeparateMode, SetMode) would decide whether these become C-real.
