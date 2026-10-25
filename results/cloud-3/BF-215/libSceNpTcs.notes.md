# libSceNpTcs (3 todo: D 3)

**What it is:** an internal PSN storage client (`Int` exports): download data, and begin/abort an upload to a new slot.

**Existing state:**
- 0 done. Three parameterless `NotImplemented_nid_no_patch` declarations: `core/libs/prx/libSceNpTcs/Export.cpp:9`, `:14`, `:19`.
- No state, no `CMakeLists.txt` (built from the glob at `core/libs/CMakeLists.txt:146`, `:159`) and no guest test.
- Debt `docs/dev/debt/libSceNpTcs.md:5`: the names and NIDs come from the PS5 3.20 export inventory, and no credible prototypes were found.
- Nothing in the tree answers these calls. The nearest code is the NpCppWebApi TitleCloudStorage `downloadData` stubs (`core/libs/prx/libSceNpCppWebApi/Export.cpp:202`), which are themselves not implemented.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 3.

## Families

- **D (3): unknown signatures over an online service.**
  - `IntDownloadData`, `IntUploadDataToNewSlotBegin` and `IntUploadDataToNewSlotAbort` need their prototypes and a PSN storage service that the tree does not model.
  - The tree has no measured "offline" error for them, so no C-real either.

## Batch proposals

- None: there are no A, B-S, B-M or C-real rows.

## Dependency notes

- Prototypes must be recovered first. A useful implementation would also need an offline answer measured on a console, since no PSN backend exists.
