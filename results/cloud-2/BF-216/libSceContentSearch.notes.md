# libSceContentSearch (4 todo: A 0, B-S 0, B-M 0, B-L 0, C 0, D 4)

Upstream main 2742004ed2.

- **What it is:** search over the console's media content library (filters on columns, sort, counts, total size, metadata by path or content id), the ContentBinder difference tracker, and an application search (count, search, the running application's index, last update id).
- **Existing state:** 19 done.
  - `g_initialized` and the 0x809D1001-0x809D100A codes at `core/libs/prx/libSceContentSearch/Export.cpp:11-18`. The host library is always empty: searches, counts and total size validate their arguments and write zero, with update id 0 (`:99-141`; debt `docs/dev/debt/libSceContentSearch.md:9`). Metadata opens throw because the missing-content error is unknown (`:93`, `:119`, debt `:10`).
  - The 8 ContentBinder exports return the constant 0x809D000F (`:149-172`); commit afbd9b882f records it as the firmware module's single constant return. The application exports were not part of that change.
  - Debt `:5`: the 4 application exports and the ContentBinder ones have no credible prototypes and are declared without parameters only to resolve imports.
  - Test: `guest_content_search_tests` (`core/libs/tests/GuestContentSearch.cpp`), built at `core/libs/prx/libSceContentSearch/CMakeLists.txt:6` and registered at `:10`.
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 4.
- **B and C families:** none.
  - The content twins have known empty-library answers, but each is written through outputs. The application forms are parameterless, so no output can be named; and unlike ContentBinder, nothing records them as constant returns in firmware.
- **D families (4):** GetApplicationLastUpdateId, GetMyApplicationIndex, GetNumOfApplication, SearchApplication. Parameterless, no prototype, outputs unknown.
- **Batch proposal:** none, because there is no A, B or C family.
- **Caveats:**
  - Once prototypes are recorded, GetApplicationLastUpdateId and GetNumOfApplication would most likely follow their content twins (`:112`, `:134`), but an application library would not be empty on a console: the running title itself is an application, so a zero count may be wrong.
