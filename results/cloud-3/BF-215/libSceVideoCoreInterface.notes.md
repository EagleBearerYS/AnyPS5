# libSceVideoCoreInterface (4 todo: D 4)

**What it is:** an interface to the system VideoCore media service: setting its function table (plain and `Unlimited`), initializing the interface and running its main loop.

**Existing state:**
- 0 done. `core/libs/prx/libSceVideoCoreInterface/Export.cpp:9-27` holds four parameterless declarations.
- No state, no CMakeLists.txt and no guest test.
- Debt:
  - `docs/dev/debt/libSceVideoCoreInterface.md:5`: `InitializeInterface`, `MainLoop` and `SetFunctionTable` have unverified signatures;
  - `:6`: `SetFunctionTableUnlimited` has no credible prototype.
- Orbital `db_types.json@b321fcdb` has no entry for them.
- The server-side library is not modelled either: all 47 exports of `core/libs/prx/libSceVideoCoreServerInterface/Export.cpp` throw.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 4.

## Families

- **D (4): unknown signature.**
  - The function-table layout is unknown.
  - So are what the interface initializes and what the main loop serves, for a service the tree does not model.

## Batch proposals

None.

## Caveats

A game title is not expected to import this library, and the tree records no title that does.
