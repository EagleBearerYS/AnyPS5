# libSceVdecSavc2 (4 todo: D 4)

**What it is:** a second-generation software AVC (H.264) decoder backend, `sceSdec*Sw2`: create, destroy, version and memory query.

**Existing state:**
- 0 done. Four parameterless `NotImplemented_nid_no_patch` declarations: `core/libs/prx/libSceVdecSavc2/Export.cpp:9`, `:14`, `:19`, `:24`.
- No state, no `CMakeLists.txt` (built from the glob at `core/libs/CMakeLists.txt:146`, `:159`) and no guest test.
- Debt `docs/dev/debt/libSceVdecSavc2.md:5`: the names and NIDs come from the PS5 3.20 export inventory, and no credible prototypes were found.
- Nothing to forward to:
  - the first-generation `sceSdec*Sw` in libSceVdecSavc are themselves parameterless todos (`core/libs/prx/libSceVdecSavc/Export.cpp:9-24`; `docs/dev/debt/libSceVdecSavc.md:5`);
  - so are the HEVC `sceSdec*SwHevc` (`docs/dev/debt/libSceVdecShevc.md:5`).

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 4.

## Families

- **D (4): unknown signatures.**
  - `CreateSw2` and `QueryMemorySw2` take a decoder configuration whose layout is unknown.
  - `DestroySw2` takes an unknown handle.
  - `GetVersionSw2` returns a version value and output convention that are not in the tree, so it cannot be C-real.

## Batch proposals

- None: there are no A, B-S, B-M or C-real rows.

## Dependency notes

- Prototypes must be recovered first, ideally together with libSceVdecSavc and libSceVdecShevc, since all three share the `sceSdec` shape. Decoding itself could use the ffmpeg-core submodule that libSceVideodec2 already links (`core/libs/prx/libSceVideodec2/CMakeLists.txt:9`).
