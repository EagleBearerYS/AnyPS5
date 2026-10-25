# libSceEditMp4 (9 todo: D 9)

**What it is:** MP4 editing: initialize with parameters and memory, edit (trim) and unite (join) files, with status polling, a finish callback, abort and terminate.

**Existing state:**
- No function is done. `core/libs/prx/libSceEditMp4/Export.cpp:9-52` holds nine parameterless throwing declarations.
- It has no CMakeLists.txt; the generic rule builds it (`core/libs/CMakeLists.txt:159`). There is no test.
- Nothing in the tree writes MP4. libSceAvPlayer only opens input (`core/libs/prx/libSceAvPlayer/src/Source.cpp:260`).

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 9.

## D families

All nine rows share one reason. The names and NIDs come from the PS5 3.20 export inventory, with no credible prototype (`docs/dev/debt/libSceEditMp4.md:5`).
- Session (5): Initialize, InitializeParam, QueryMemSize, Terminate, Abort.
- Operations (2): Edit, Unite.
- Progress (2): GetStatus, SetFinishCallback.

With prototypes, Edit and Unite would be B-L: an MP4 (ISO/IEC 14496-12/14) writer is a new subsystem here.

## Batch proposal

None.

## Caveats

None beyond the missing prototypes.
