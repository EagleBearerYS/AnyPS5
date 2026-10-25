# libSceAudioLatencyEstimation (4 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 4)

**What it is:** by its names, an audio-latency estimator that takes data, reports a delay time and has its own initialize and release calls (sysmodule `0x000000e8`, `core/libs/prx/libSceSysmodule/ModuleTable.hpp:119`). No tree file describes it further.

**Existing state:**
- Built: no `CMakeLists.txt` of its own, so `core/libs/CMakeLists.txt:159` compiles `core/libs/prx/libSceAudioLatencyEstimation/Export.cpp`.
- 0 done. Four parameterless declarations (`Export.cpp:9`, `:14`, `:19`, `:24`; `include/Api.hpp:7-10`). There is no state, no error constant and no test.
- Debt (`docs/dev/debt/libSceAudioLatencyEstimation.md:5`):
  - the names come from the aerolib list and are assigned to this module by their prefix;
  - the library is absent from the PS5 3.20 inventory, so the pairing is not firmware-verified;
  - "no credible prototypes were found... callers must not call them until signatures are verified".

## Families

- **D (4): AddData, GetDelayTime, Initialize, Release.**
  - No prototype in the tree or a cited source.
  - No neighbour anywhere: no other library on main has a `LatencyEstimation` name (`in/main-names.json`).
  - The data AddData takes, the unit of the delay and the estimator itself are undescribed.

## Batch proposal

None.

## Caveats

- The sysmodule table names the module (`ModuleTable.hpp:119`), but the debt says its export list is not firmware-verified. Even the module-to-NID pairing is unconfirmed.
