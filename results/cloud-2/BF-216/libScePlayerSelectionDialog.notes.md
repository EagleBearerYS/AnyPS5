# libScePlayerSelectionDialog (1 todo: D 1)

- **What it is:** the PS5 common dialog in which a user picks players (accounts) for a session.
- **Existing state** (`core/libs/prx/libScePlayerSelectionDialog/Export.cpp`):
  - 7 done on the common-dialog state machine (`g_status` `:25`).
    - Open (`:37`) fully validates `PlayerSelectionDialogParam` against the logged-in users, then finishes at once (`:54`).
    - GetResult (`:73`) reports USER_CANCELED.
    - Close (`:58`) returns NOT_RUNNING when not running.
  - The layouts are asserted in `libSceCommonDialog/CommonDialog.hpp:118-156`. The base-parameter check is `ValidateCommonDialogBaseParam` (`:246`).
  - The debt files the dialog under Silent stubs (`docs/dev/debt/libScePlayerSelectionDialog.md:5`).
  - Test: `guest_player_selection_dialog_tests` (`core/libs/tests/GuestPlayerSelectionDialog.cpp`), built at `libScePlayerSelectionDialog/CMakeLists.txt:7` and registered at `:11`.
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 1.
- **Functions that already behave but count as todo:** `scePlayerSelectionDialogParamInitialize` already rejects null. Only the non-null branch (`:84`) throws.
- **D:** that branch must write the console's default values.
  - Zeroing, the base size and magic, and the 216-byte size can all be derived from the tree.
  - The defaults of userId, maxPlayers and mode are unverified (debt `:9`). Open rejects `userId == -1`, so the default userId matters.
- **Batch proposal:** none.
- **Caveats:** if the defaults are measured, the row becomes B-S next to Open's validation. A test would show that ParamInitialize followed by setting userId to a logged-in user gives a parameter Open accepts.
