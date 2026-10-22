# libSceFont (69 todo: B-M 3, D 66)

What it is: Sony's font library, covering text sources, strings, writing lines, glyph metrics and glyph rasterization. It renders with FreeType through `libSceFontFt` (3rdparty/freetype submodule), and 126 exports are done (`src/Open.cpp`, `Render.cpp`, `Style.cpp`, `Text.cpp`, `WritingLine.cpp`).

The todo list is dominated by the separate "Font Graphics" vector fill renderer (`sceFontGraphics*` plus graphics device/service creation). Its descriptor structs are partly recovered (`src/Unimplemented.cpp:27-135`: region, fill method, fill plot, fill rates, device usage). Its frame, resource, design and canvas objects and the renderer behind them are opaque.

Families:
- B-M (3): `sceFontGlyphRenderImage` and its `Horizontal`/`Vertical` forms.
  - The signature is recovered (debt), and the surface, metrics and output layouts are already used.
  - A glyph from `sceFontGenerateCharGlyph` (`Render.cpp:371`) keeps its owner font, code point and scale, so rendering it reuses `RenderCharGlyphImageCore` and `RenderDirectional` (`Render.cpp:97`, `:220`).
  - Error precedence and the style-frame override are unverified.
- Graphics device/service (6), frame/resource/canvas objects (16), resource Setup*/Update* (24): D. The layouts and draw semantics are unrecovered.
- Graphics descriptor init/setters (11): D. Their layouts are in the tree, but the tag and flag values and the meaning of the layout selectors and mappings are not, and their only consumer is the absent renderer.
- Opaque-output queries `GetFontMetrics`, `GetFontStyleInformation`, `GetFontGlyphsOutlineProfile`, `GetGlyphExpandBufferState` (4): D. FreeType could supply the values, but the record layouts are unknown.
- Words (3), `sceFontControl`, `sceFontSupportGlyphs`: D. The object layouts, word-break rules and command ids are unknown.

Third-party:
- The B-M rows need nothing new; freetype is already a submodule.
- If the Words API is ever recovered, word segmentation would want Unicode UAX #29/#14 data (ICU or libunibreak), but the library's own rules are unknown, so no dependency is warranted now.
- The Graphics renderer would need a 2D fill rasterizer. FreeType's outline rasterizer covers glyph fills but not shape or texture fills. Its semantics are unknown, so none is proposed.

Batch proposals (no batch of 10 exists among the A/B/C rows):
- "Glyph-object rendering" (3 functions). Test: extend `core/libs/tests/GuestFont.cpp` (ctest `guest_font`). Generate a glyph, render it with each of the three calls, and compare its pixels and metrics with `sceFontRenderCharGlyphImage*` for the same code and scale. Add an invalid-glyph case and a null-output case.
