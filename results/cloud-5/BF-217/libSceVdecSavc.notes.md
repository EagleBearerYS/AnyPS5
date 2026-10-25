# libSceVdecSavc (4 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 4)

**What it is:** a software AVC (H.264) video decoder, the `sceSdec*Sw` family: create, destroy, version and memory query.

**Existing state** (`core/libs/prx/libSceVdecSavc/Export.cpp`; there is no CMakeLists, so `core/libs/CMakeLists.txt:159` builds `Export.cpp`):
- 0 done. Four parameterless bodies throw (`:9-27`).
- No state and no error constants.
- Debt (`docs/dev/debt/libSceVdecSavc.md:5`): the names are in the 3.20 export inventory, and "no credible prototypes were found".
- No tests.
- Neighbours:
  - The other Sdec modules are all todo: libSceVdecSavc2 (`sceSdec*Sw2`) and libSceVdecShevc (`sceSdec*SwHevc`).
  - libSceVdecsw is done, but it is a different API (`sceVdecsw*`) with no shared name.

## Families

- **D (4):** CreateSw, DestroySw, GetVersionSw and QueryMemorySw. There is no prototype.
  - GetVersionSw's value is unknown.
  - QueryMemorySw's sizes are unknown.

## Batch proposal

None. No claims.

## Caveats

- Even with prototypes, the decoder behind Create is a missing subsystem (B-L). Only GetVersion and QueryMemory could be smaller, and only with measured values.
