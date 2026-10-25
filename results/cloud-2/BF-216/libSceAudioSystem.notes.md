# libSceAudioSystem (2 todo: D 2)

**What it is:** system-level suspend and resume of the console's audio service.

**Existing state:**
- No function is done. `core/libs/prx/libSceAudioSystem/Export.cpp:9-17` holds two parameterless throwing declarations.
- It has no CMakeLists.txt; the generic rule builds it (`core/libs/CMakeLists.txt:159`). There is no test.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 2.

## D families

Both rows: the names and NIDs come from the PS5 3.20 export inventory, with no credible prototype (`docs/dev/debt/libSceAudioSystem.md:5`).

Suspend and resume act on a system service whose arguments and game-process result are unknown. A constant success would be a silent stub, and no title needs one.

## Batch proposal

None.

## Caveats

None.
