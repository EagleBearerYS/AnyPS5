# libSceWkFontConfig (5 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 5)

**What it is:** the WebKit font configuration: font attributes, fonts by name, and font names and paths from the system font set.

**Existing state:**
- No own `CMakeLists.txt`. `core/libs/prx/libSceWkFontConfig/Export.cpp` is built by `core/libs/CMakeLists.txt:159`, so the rows are exported.
- 0 done. No state and no error constants; `APS5_DUMMY_FUN` (`Export.cpp:9`) adds only the dummy export.
- The sysmodule table names it as user id 0x000000e9 (`core/libs/prx/libSceSysmodule/ModuleTable.hpp:120`).
- Debt: `docs/dev/debt/libSceWkFontConfig.md:5` lists the 5 exports from the PS5 3.20 table, cross-checked against 13.60, with unverified signatures and behaviour.
- No tests.
- No neighbour twin. The font libraries (libSceFont, libSceFreeType) use different APIs.

## Families

- **D (5):** `sceWkFontConfigGetAttr` (`:10`), `sceWkFontConfigGetFont` (`:15`), `sceWkFontConfigGetFontFromFontName` (`:20`), `sceWkFontConfigGetFontName` (`:25`) and `sceWkFontConfigGetFontPath` (`:30`).
  - They have no prototype, no font-table model and no public source.
  - Paths would name console system font files.

## Batch proposal

None.

## Caveats

- PR 8747 (closed) only declared these names.
