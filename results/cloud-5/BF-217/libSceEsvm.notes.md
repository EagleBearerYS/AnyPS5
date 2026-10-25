# libSceEsvm (3 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 3)

**What it is:** three Esvm media-stack calls: an app audio-sync context, its flush, and a video sync. Nothing in the tree or in a public source says what they do.

**Existing state:**
- No own `CMakeLists.txt`. `core/libs/prx/libSceEsvm/Export.cpp` is built by the generic rule at `core/libs/CMakeLists.txt:159`, so the rows are exported.
- 0 done. No state and no error constants; `APS5_DUMMY_FUN` (`Export.cpp:9`) adds only the dummy export.
- Debt: `docs/dev/debt/libSceEsvm.md:5` says the signatures and behaviour are unverified and the declarations only resolve imports.
- No tests.
- Neighbours: libSceEsvmEngine, the media-key engine with 19 rows, all todo (`docs/dev/debt/libSceEsvmEngine.md:5`), and libSceAgcDriver's `sceAgcDriverIsEsvmProcess`, also todo. None of them is done.

## Families

- **D (3):** `sceEsvmAppAudioSyncCreateContext` (`:14`), `sceEsvmAppAudioSyncFlush` (`:19`) and `sceEsvmSyncVideo` (`:24`).
  - All three are parameterless declarations with no prototype, no done twin and no recorded answer.

## Batch proposal

None.

## Caveats

- A title import that shows the argument registers would be the first evidence. Until then, any answer would be a guess.
