# libScePadTracker (6 todo: D 6)

**What it is:** controller tracking through the PlayStation Camera. It finds the pad's light bar in camera frames: initialize with working memory, calibrate, update per frame, read the tracked state, and terminate.

**Existing state:**
- 0 done. Every export is a parameterless declaration that throws (`core/libs/prx/libScePadTracker/Export.cpp:9-37`, one every 5 lines).
- There is no `CMakeLists.txt` in the library: it is built by the generic loop in `core/libs/CMakeLists.txt:159`. There are no tests, state objects or error constants.
- Debt `docs/dev/debt/libScePadTracker.md:5`: the names and NIDs come from the PS5 3.20 export inventory, and no credible prototypes were found.
- Neighbours:
  - libScePad's tracker-side calls throw as well, for example `scePadReadForTracker` (`core/libs/prx/libScePad/Export.cpp:931`).
  - libSceCamera answers `sceCameraIsAttached` (`core/libs/prx/libSceCamera/Export.cpp:11`), but `sceCameraOpen` throws (`:16`), so no camera frames exist to track.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 6.

## Families

- **D (6), unknown signature:** all six are parameterless declarations with no credible prototype (debt `:5`).
  - `GetWorkingMemorySize` returns a console constant that nobody has measured.
  - `Init`, `Update`, `ReadState` and `Calibrate` take records and camera frames that are unknown.
  - `Term`'s answer without `Init` is unknown.
  - There is no PadTracker error code in the tree that an "absent camera" answer could reuse.

## Batch proposals

None. No row is A, B or C-real.

## Dependency notes

- An implementation needs the prototypes, the PadTracker error codes and a camera source. With no camera, the realistic outcome is a measured "no camera" or "not tracking" answer, which the tree does not have.
