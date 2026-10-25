# libSceSaveData.native (3 todo: A 0, B-S 0, B-M 0, B-L 0, C 0, D 3)

Upstream main 2742004ed2.

- **What it is:** the PS5 save-data library the titles link: initialize/terminate, Mount3 and the PS4-style Mount, Umount/Umount2, search, params, icons, delete, backup, convert, save-data memory, transactions, and the PS4-to-PS5 transferring mount.
- **Existing state:** 42 done.
  - Mounts live in `g_slots` (`core/libs/prx/libSceSaveData.native/SaveData.hpp:52`), host directories under `_sd` (`Export.cpp:28`). Only the running title's saves are kept; a search with another title's `title_id` reports zero hits (debt `docs/dev/debt/libSceSaveData.native.md:18`).
  - `sceSaveDataPrepare` returns OK without checks (`Export.cpp:670`). `sceSaveDataTransferringMount(Ps4)` always returns NOT_FOUND, because there is no PS4 save area (`:884`, debt `:7`).
  - Tests: six guest executables in `core/libs/prx/libSceSaveData.native/CMakeLists.txt`, including `guest_savedata_ps4_tests` (`core/libs/tests/GuestSaveDataPs4.cpp`, built at `:43`, registered at `:47`; `TestMountPs4` at `GuestSaveDataPs4.cpp:100`) and `guest_savedata_cancel_tests` (`core/libs/tests/GuestSaveDataCancel.cpp`, built at `:53`, registered at `:57`), which asserts that Cancel throws "not implemented".
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 3.
- **B and C families:** none.
- **D (3):**
  - `sceSaveDataCancel(uint32)`: the argument goes to the save-data service and its meaning needs the service handler (debt `:10`).
  - `sceSaveDataPrepareForTransferring`: same transport as Prepare, but needs the service backend (debt `:6`). If it only accepts transferring mounts, it could never see a mounted target here, since transferring mounts always fail; that is not recorded.
  - `sceSaveDataMount`, partial: only a non-null `title_id` throws (`Export.cpp:1057`); every other path forwards to Mount3. The gap is another title's save. The host keeps no other title's saves, so a read-only mount would most likely be NOT_FOUND, consistent with the zero-hit search (debt `:18`); but whether a create or read-write mount of another title's save is allowed, and with what error, is not in the tree.
- **Batch proposal:** none, because there is no A, B or C family.
- **Caveats:**
  - The input file gives lines 983, 1047 and 994 for these three; in the tree at 2742004ed2 they are at `Export.cpp:991` (Cancel), `:1055` (Mount) and `:1002` (PrepareForTransferring). The rows cite the tree.
  - The Mount gap could be narrowed to the create and read-write modes if the read-only NOT_FOUND answer were accepted, but the function would still throw, so it would stay todo.
