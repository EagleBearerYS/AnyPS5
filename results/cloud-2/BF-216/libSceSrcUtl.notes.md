# libSceSrcUtl (9 todo: D 9)

**What it is:** a sample-rate-converter utility: handle sizing, initialize and finalize, resample and flush, the filter delay, and a processing priority.

**Existing state:**
- No function is done. `core/libs/prx/libSceSrcUtl/Export.cpp:9-52` holds nine parameterless throwing declarations.
- It has no CMakeLists.txt; the generic rule builds it (`core/libs/CMakeLists.txt:159`). There is no test.
- The only resampler in the tree is AJM's 4-point Catmull-Rom (`core/libs/prx/libSceAjm.native/src/Ajm.cpp:634`, `:638`). Its interpolation is an assumption there too (`docs/dev/debt/libSceAjm.native.md:19`).

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 9.

## D families

All nine rows share one reason. The names and NIDs come from the PS5 3.20 export inventory, with no credible prototype (`docs/dev/debt/libSceSrcUtl.md:5`).
- Handle lifecycle (4): GetHandleSize, Initialize, InitializeEx, Finalize.
- Processing (3): Resample, Flush, GetDelaySamples.
- Priority (2): GetPriority, SetPriority.

With prototypes, Resample, Flush and GetDelaySamples would still depend on the console's filter, which no source gives.

## Batch proposal

None.

## Caveats

None beyond the missing prototypes.
