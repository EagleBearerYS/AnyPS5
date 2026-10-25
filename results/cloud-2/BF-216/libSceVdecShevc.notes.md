# libSceVdecShevc (7 todo: D 7)

**What it is:** the "Sdec" software HEVC decoder interface: query memory, create, execute, get frame, reset, destroy, and a version query.

**Existing state:**
- No function is done. `core/libs/prx/libSceVdecShevc/Export.cpp:9-42` holds seven parameterless throwing declarations.
- It has no CMakeLists.txt; the generic rule builds it (`core/libs/CMakeLists.txt:159`). There is no test.
- HEVC decoding exists in libSceVideodec2 through FFmpeg (`core/libs/prx/libSceVideodec2/Export.cpp:280`, `:289`). libSceVdecsw is built on it.
- The sibling software AVC library, libSceVdecSavc, is all todo too (0 done, 4 todo in `main-libraries.json`), so it offers no Sdec neighbour.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 7.

## D families

All seven rows share one reason. The names and NIDs come from the PS5 3.20 export inventory, with no credible prototype (`docs/dev/debt/libSceVdecShevc.md:5`).

With prototypes and records, they could be B-M on the Videodec2 decoder, as libSceVdecsw already is (`core/libs/prx/libSceVdecsw/Export.cpp:105`).

## Batch proposal

None.

## Caveats

None beyond the missing prototypes.
