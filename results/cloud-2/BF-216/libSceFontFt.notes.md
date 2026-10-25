# libSceFontFt (7 todo: A 0, B-S 0, B-M 0, B-L 0, C 0, D 7)

Upstream main 2742004ed2.

- **What it is:** the FreeType back end of libSceFont. It exports the font-library driver table and renderer selection that libSceFont calls through, per-format "support" switches, a format-mask switch, a system-fonts switch, font aliases and a glyph-selection table.
- **Existing state:** 35 done.
  - `sceFontSelectLibraryFt` and `sceFontSelectRendererFt` return the in-tree driver and renderer tables for selector 0 (`core/libs/prx/libSceFontFt/Export.cpp:12`, `:16`); the 21 `Library*`/`Renderer*` driver entries live in `src/Driver.cpp` (table built at `:784-801`).
  - The 12 per-format switches (`Export.cpp:35-91`) call `FontFt::SupportFormats` (`src/Driver.cpp:817`): magic check, library lock, then `FT_Get_Module` for named FreeType modules, `SCE_FONT_ERROR_NO_SUPPORT_FORMAT` when one is missing. Debt `docs/dev/debt/libSceFontFt.md:5`: the host registers FreeType's default modules at creation instead of enabling formats one by one.
  - The driver's own `support_formats` hook, `LibrarySupport` (`src/Driver.cpp:200`), ignores its format mask. libSceFont's `sceFontSupportSystemFonts` passes it 0x52 (`core/libs/prx/libSceFont/src/Library.cpp:420`).
  - Debt `:10`: alias init/term take a handle pointer, alias registration takes a string and a font ID or path, and glyph selection returns "a table pointer whose layout remains unresolved".
  - FontFt is tested through libSceFont's `guest_font_tests` (`core/libs/tests/GuestFont.cpp:312-328` covers the per-format switches), built at `core/libs/prx/libSceFont/CMakeLists.txt:5` and registered at `:9`.
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 7.
- **B and C families:** none.
  - No row is the same as a done one. `SupportFontFormats(library, formats)` has the driver hook's shape, but the hook ignores the mask, while the exported switches check modules; forwarding to the hook would report support for any mask.
- **D families (7):**
  - Aliases (4): InitAliases, TermAliases, SetAliasFont, SetAliasPath. `FontFtAliases` is opaque (`include/Api.hpp:7`); the setters take no handle, so which table they write is unknown, and nothing in libSceFont's open path consults aliases.
  - Format mask (1): SupportFontFormats. The mask's bit meanings are not recorded anywhere; 0x52 is the only value in the tree.
  - System fonts (1): SupportSystemFonts. The SST system fonts are `.otf` files (`core/libs/prx/libSceFont/src/SystemFont.cpp:49`), but that fixes neither the module list nor whether this switch also sets up the system-font context as libSceFont's does (`Library.cpp:419`).
  - Glyph selection (1): sceFontSelectGlyphsFt. Table layout and valid selectors unknown.
- **Batch proposal:** none, because there is no A, B or C family.
- **Caveats:**
  - If the 0x52 mask were decoded (bit per format), SupportFontFormats and SupportSystemFonts would become B-S on `FontFt::SupportFormats` with the existing tests; that decoding needs the console library.
  - `Export.cpp:2-3` carry a shadPS4 copyright header; the tree does not cite shadPS4 for any of these seven behaviours.
