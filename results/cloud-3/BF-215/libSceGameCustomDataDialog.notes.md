# libSceGameCustomDataDialog (2 todo: D 2)

**What it is:** the common dialog that shows PSN game custom data (Initialize, Open, UpdateStatus, GetStatus, GetResult, Close, Terminate).

**Existing state:**
- 7 done. The common-dialog state machine is `g_status` (`core/libs/prx/libSceGameCustomDataDialog/Export.cpp:22`). Its error constants are at `:15-19`: `NOT_INITIALIZED` 0x80B80003, `ALREADY_INITIALIZED` 0x80B80004, `NOT_FINISHED` 0x80B80005, `BUSY` 0x80B80007 and `ARG_NULL` 0x80B8000D.
- `sceGameCustomDataDialogOpenA` (`:34`) finishes at once. `sceGameCustomDataDialogGetResultA` (`:51`) writes only the leading `int32_t` result, `USER_CANCELED`.
- These are silent stubs by design (debt `docs/dev/debt/libSceGameCustomDataDialog.md:5`).
- Test: `guest_game_custom_data_dialog_tests` (`core/libs/tests/GuestGameCustomDataDialog.cpp`), registered at `core/libs/prx/libSceGameCustomDataDialog/CMakeLists.txt:6` and `:9`.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 2.

## Families

- **D (2), unknown signature:** `Open` and `GetResult` are parameterless declarations (`:72`, `:77`). Debt `:9` says Orbital has prototypes only for the `A` variants, and none credible were found for these.
  - Their answers need arguments: `Open` takes a parameter and `GetResult` writes a result.
  - Forwarding to `OpenA` and `GetResultA` would assume that the non-A parameter and result layouts equal the `A` ones, and nothing in the tree says so.

## Batch proposals

None. No row is A, B or C-real.

## Caveats

- If a title's call site shows that the non-A calls take the same pointers as the `A` ones, they become A rows: forward to `:34` and `:51`, and extend `guest_game_custom_data_dialog_tests`.
