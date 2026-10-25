# libSceAc3Enc (7 todo: D 7)

**What it is:** an AC-3 (Dolby Digital) audio encoder: create and delete an encoder, encode synchronously or asynchronously, flush, poll and wait.

**Existing state:**
- 0 done. Every export is a parameterless import declaration that throws (`core/libs/prx/libSceAc3Enc/Export.cpp:9-42`).
- There is no `CMakeLists.txt` in the library: it is built by the generic loop in `core/libs/CMakeLists.txt:159`. There are no tests, state objects or error constants.
- Debt `docs/dev/debt/libSceAc3Enc.md:5`: the names and NIDs come from the PS5 3.20 export inventory, and no credible prototypes were found.
- No other library in the tree implements or calls an AC-3 encoder. The only other reference is the module table entry in `core/libs/prx/libSceSysmodule/ModuleTable.hpp`.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 7.

## Families

- **D (7), unknown signature:** all seven are parameterless declarations with no credible prototype (debt `:5`). Every call either needs an encoder handle and buffers or returns one, so no answer can be given without arguments.

## Batch proposals

None. No row is A, B or C-real.

## Dependency notes

- An implementation would need the prototypes (from a title's call sites or a module disassembly) and an AC-3 encoder. `3rdparty/ffmpeg-core` is already a submodule, added at the root `CMakeLists.txt:326`. Whether its configuration includes the `ac3` encoder was not checked, because the submodule is not checked out in this tree.
