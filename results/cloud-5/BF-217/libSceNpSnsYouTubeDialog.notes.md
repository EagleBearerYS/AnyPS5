# libSceNpSnsYouTubeDialog (7 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 7)

**What it is:** the system YouTube share/link dialog: a common dialog (Initialize, Open, UpdateStatus, GetStatus, GetResult, Close, Terminate).

**Existing state:**
- Nothing is done. `core/libs/prx/libSceNpSnsYouTubeDialog/Export.cpp:9-42` holds seven parameterless declarations, each of which throws.
- There is no `CMakeLists.txt`, so `Export.cpp` is built by `core/libs/CMakeLists.txt:159`.
- Debt: `docs/dev/debt/libSceNpSnsYouTubeDialog.md:5` says no credible prototypes were found and callers must not call these until signatures are verified.
- Tests: none registered.
- Sibling: libSceNpSnsFacebookDialog has the same seven names, all done (`core/libs/prx/libSceNpSnsFacebookDialog/Export.cpp:28-70`).
  - It runs the common-dialog state machine and reports "user cancelled" (`:56-57`).
  - Its errors are the common-dialog constants (`:15-19`).
  - Its signatures come from Orbital's type database (`https://github.com/AlexAltea/orbital/blob/b321fcdbd700cc02da1cb1ad25412781d5ec73c6/tools/ida/db_types.json#L1602-L1608`).
  - Its debt files it under **Silent stubs** (`docs/dev/debt/libSceNpSnsFacebookDialog.md:5`).
  - Its test `guest_np_sns_facebook_dialog` is registered at `libSceNpSnsFacebookDialog/CMakeLists.txt:9`.

## Families

- **D (7).**
  - No source gives a YouTube prototype. Orbital lists the Facebook dialog but no YouTube names.
  - The only in-tree model is a silent stub. Copying it would guess both the arguments (Open's parameter record, GetResult's result record) and the answers.
  - It is not A: these are separate entry points with their own state, not aliases of the Facebook ones.

## Batch proposal

None. No PR claims these rows.

## Caveats

- If a title needs the dialog, the cheapest honest route is a C-silent copy of the Facebook model, recorded under Silent stubs with that title.
  - Initialize, UpdateStatus, GetStatus, Close and Terminate read no argument in that model.
  - Open and GetResult would still need the YouTube parameter and result records, at least their sizes.
