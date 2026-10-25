# libSceVideoDecoderArbitration (4 todo: D 4)

**What it is:** arbitration of the hardware video decoder between processes: initialize, enable, a suspend mode, and accepting arbitration events.

**Existing state:**
- No function is done. `core/libs/prx/libSceVideoDecoderArbitration/Export.cpp:9-27` holds four parameterless throwing declarations.
- It has no CMakeLists.txt; the generic rule builds it (`core/libs/CMakeLists.txt:159`). There is no test.
- The sibling libSceVideoArbitration is all todo too (0 done, 3 todo in `main-libraries.json`).

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 4.

## D families

All four rows share one reason. The names and NIDs come from the PS5 3.20 export inventory, with no credible prototype (`docs/dev/debt/libSceVideoDecoderArbitration.md:5`).

The behaviour is a system-side sharing protocol whose events and results for a game process are unknown. No C answer exists: the declarations are parameterless, and no console answer is recorded.

## Batch proposal

None.

## Caveats

None beyond the missing prototypes.
