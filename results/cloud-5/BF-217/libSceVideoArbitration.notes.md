# libSceVideoArbitration (3 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 3)

**What it is:** a system video-arbitration client: initialize, enable, accept an event. Nothing in the tree says what is arbitrated between whom.

**Existing state:**
- No own `CMakeLists.txt`. `core/libs/prx/libSceVideoArbitration/Export.cpp` is built by `core/libs/CMakeLists.txt:159`, so the rows are exported.
- 0 done. No state and no error constants.
- Debt: `docs/dev/debt/libSceVideoArbitration.md:5` says no credible prototypes were found.
- No tests.
- Neighbours, none done:
  - libSceVideoDecoderArbitration has the same three names plus `EnableSuspendMode`, all todo (`docs/dev/debt/libSceVideoDecoderArbitration.md:5`, table id 0x000000fc at `core/libs/prx/libSceSysmodule/ModuleTable.hpp:137`);
  - libSceAudioOut's arbitration calls are todo too.

## Families

- **D (3):** `sceVideoArbitrationAcceptEvent` (`:9`), `sceVideoArbitrationEnable` (`:14`) and `sceVideoArbitrationInitialize` (`:19`).
  - They have no prototype, no event model and no recorded answer.

## Batch proposal

None.

## Caveats

- A constant-success Initialize or Enable would be C-silent and would need a title. None is recorded.
