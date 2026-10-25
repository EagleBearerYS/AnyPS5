# libSceVdecCore.native (4 todo: D 4)

**What it is:** the native module of the video decoder core. Only four exports are declared here: the BID and SVP decoder creation variants, a memory map, and SVP decode input. The other 20 names of its PS5 3.20 export table are declared under libSceVdecCore (`docs/dev/debt/libSceVdecCore.native.md:7`).

**Existing state:**
- No function is done. `core/libs/prx/libSceVdecCore.native/Export.cpp:9-27` holds four parameterless throwing declarations.
- It has no CMakeLists.txt; the generic rule builds it (`core/libs/CMakeLists.txt:159`). There is no test.
- libSceVdecCore's 20 names are all todo as well (`main-libraries.json`), so they offer no neighbour.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 4.

## D families

All four rows share one reason. The names and NIDs come from the PS5 3.20 export inventory, with no credible prototype (`docs/dev/debt/libSceVdecCore.native.md:5`).
- Creation (2): BID and SVP variants.
- Memory (1): MapMemory.
- SVP input (1): SetDecodeInputSvp.

What "BID" and "SVP" mean here is not recorded in the tree.

## Batch proposal

None.

## Caveats

None beyond the missing prototypes.
