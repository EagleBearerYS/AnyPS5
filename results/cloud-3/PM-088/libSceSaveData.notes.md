# libSceSaveData (66 todo: A 2, B-S 3, B-M 2, D 59)

What it is: the host save-data library backed by `SaveDataCore.hpp` (file-backed mounts, events, save-data memory, transactions). It runs next to `libSceSaveData.native`, which implements Mount3, Umount2, search, params, icons, Delete and Convert.

Comparison with .native: none of the 66 todo names is implemented in `libSceSaveData.native` (its own todo is only Cancel and PrepareForTransferring), so there is no by-name A.

The state is split, and this matters for every mount query: `sceSaveDataMount3` mounts live in .native (`SaveData.hpp:55`, `g_slots`), while SaveDataCore keeps separate slots (TransferringMount only), events and memory. Also, 0x809F0005 is `SD_ERROR_NO_PERMISSION` in `SaveDataCore.hpp:49` but `SAVE_DATA_ERROR_ALREADY_INITIALIZED` in `.native/SaveData.hpp`.

Classes:
- A (2):
  - `sceSaveDataAbort` → `savedata::abort_internal` (`SaveDataCore.hpp:1209`); zero-argument as declared, signature unverified.
  - `sceSaveDataInitialize2` → `initialize_internal`, exactly as `sceSaveDataInitialize3` (`Export.cpp:31`); the init block is ignored.
- B-S (3) and B-M (2): `GetProgress(float*)`, `ClearProgress()`, `UmountWithBackup(mountPoint*)`, and `CheckBackupData` / `RestoreBackupData` with their parameter blocks.
  - The signatures follow shadPS4's PS4 bindings: public but reverse-engineered, so the PS5 ABI must be confirmed from a call site.
  - The backup pair is B-M because `backup_internal` (`:1098`) only queues an event and keeps no backup copy.
  - `UmountWithBackup` must unmount through .native's slots (`sceSaveDataUmount2`, `.native/Export.cpp:911`).
- D (59):
  - PSN cloud/upload/download (17): no cloud or PSN result among the 0x809F00xx constants.
  - System administration (22): other users and titles, USB database, root paths, Mount/UmountSys.
  - Dialog/light backup variants (5); events/callback (4): the State fields exist, the contract does not; unknown semantics (7).
  - `Mount5`: no struct in the tree.
  - `IsMounted`, `GetMountedSaveDataCount`, `GetAllSize`: helpers exist (`:833`, `:844`, `:1314`), but the signatures are unverified and the helpers read the wrong half of the split state.

Third-party: none. `std::filesystem` covers a backup store, and mbedtls is not needed because host saves are not encrypted.

Batch proposal: "local backup and progress", 7 functions (Abort, Initialize2, GetProgress, ClearProgress, UmountWithBackup, CheckBackupData, RestoreBackupData). This is below 10, but it is the whole A/B set; first confirm the 5 shadPS4-derived signatures on a title.
- Test: a new executable `savedata_backup_tests` in `core/libs/CMakeLists.txt` beside `savedata_core_write_replacement_tests` (line 598), linking both savedata libraries.
- Backup/restore round trip: Check finds the backup, and Restore undoes a later change.
- Check with no backup returns NOT_FOUND; progress is checked after Backup and after Clear.
- UmountWithBackup queues `UMOUNT_BACKUP_END`, which `sceSaveDataGetEventResult` reads; Abort drops pending events.
