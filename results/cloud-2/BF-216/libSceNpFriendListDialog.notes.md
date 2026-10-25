# libSceNpFriendListDialog (2 todo: D 2)

- **What it is:** the PSN friend-list common dialog.
- **Existing state** (`core/libs/prx/libSceNpFriendListDialog/Export.cpp`):
  - 7 done on the common-dialog state machine. `g_status` is at `:22`.
    - Initialize (`:28`), UpdateStatus (`:43`), GetStatus (`:47`), Close (`:61`) and Terminate (`:67`) manage the status.
    - OpenA (`:34`) finishes at once.
    - GetResultA (`:51`) writes USER_CANCELED into the leading int32.
  - The debt files this under Silent stubs (`docs/dev/debt/libSceNpFriendListDialog.md:5`). The A-form signatures are from Orbital.
  - Test: `guest_np_friend_list_dialog_tests` (`core/libs/tests/GuestNpFriendListDialog.cpp`), built at `libSceNpFriendListDialog/CMakeLists.txt:6` and registered at `:9`.
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 2.
- **D:** the non-A `Open` and `GetResult` are parameterless 3.20 declarations (debt `:9`).
  - GetResult's answer is written through its argument.
  - Open's A-form rejects a null parameter before finishing. Without a prototype, a null parameter would get a made-up success.
- **Batch proposal:** none.
- **Caveats:** if the non-A prototypes match the A-forms (one parameter or result pointer), both become two-line forwards (A) onto OpenA and GetResultA. Debt `:9` must then be updated.
