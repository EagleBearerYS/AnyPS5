# libSceShareUtility (22 todo: C-silent 12, D 10)

**What it is:** the share-menu client:
- library init and terminate, in plain and `Advance` forms;
- opening or waking the system share menu (by content, broadcast or default);
- upload-data and playing-content records;
- share-service queries.

**Existing state:**
- 0 done. `core/libs/prx/libSceShareUtility/Export.cpp:9-117` holds 22 parameterless `NotImplemented_nid_no_patch` declarations.
- No state and no guest test.
- No CMakeLists.txt: the library is built by the generic loop (`core/libs/CMakeLists.txt:158-161`).
- Debt `docs/dev/debt/libSceShareUtility.md:5` finds no credible prototype for any of the 22. That is stale for 12 of them: Orbital `db_types.json@b321fcdb` gives them at L2102-L2113. The tree already cites that file at `docs/dev/debt/libSceNpFriendListDialog.md:5`.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 12, D 10.

## Families

- **C-silent (12): "silent stub, needs a title".**
  - **Lifecycle (5):** `Initialize` (L2108), `InitializeEx2` (L2109), `AdvanceInitialize` (L2102), `Terminate` (L2113) and `AdvanceTerminate` (L2104).
    - They set up a heap and a thread for a share service the tree does not model, and nothing in the tree observes them.
    - The console errors for a double init, or a call with no init, are unknown.
  - **Share menu (6):** `OpenShareMenu` (L2110), `OpenShareMenuBroadcast` (L2111), `OpenShareMenuDefault` (L2112), `AdvanceWakeUpShareMenu` (L2105), `AdvanceWakeUpShareMenuBroadcast` (L2106) and `AdvanceWakeUpShareMenuDefault` (L2107).
    - They open a system UI the tree does not have, and return only a status.
    - The tree shows no system UI elsewhere either: its common dialogs report a cancel (`docs/dev/debt/libSceNetCtlApDialog.md:5`).
  - **Setter (1):** `AdvanceSetUploadContentData` (L2103) stores data that no modelled service reads.
- **D (10): unknown signature, with no Orbital entry either.**
  - The rows: `AdvanceFromFileWithContentIdList`, `AdvanceFromFileWithTitleIdList`, `AdvanceGetServiceInfo`, `AdvanceQueryServiceInfo`, `AdvanceQueryServiceInfoA`, `AdvanceResetPlayingContent`, `AdvanceSetPlayingContent`, `AdvanceValidateContents`, `AdvanceWakeUpShareMenuForShareContent` and `OpenShareMenuForShareContent`.
  - The layouts of their content lists, playing-content records, service info and share-content parameters are unknown.

## Batch proposals

None required: no row is A, B or C-real.

If a title imports the share calls (with an `Unblocks:` line), the C-silent rows can go in one batch:

1. **"feat(libSceShareUtility): offline share-menu silent stubs" (12).**
   - **Changes:**
     - declare the Orbital prototypes in `core/libs/prx/libSceShareUtility/include/Api.hpp`;
     - each call returns 0 and keeps nothing;
     - in the debt, record them under "Silent stubs" and narrow the "no credible prototype" entry to the 10 D rows.
   - **Tests:** a new `guest_share_utility_tests` (`core/libs/tests/GuestShareUtility.cpp`).
     - Register it in a new `core/libs/prx/libSceShareUtility/CMakeLists.txt`, modelled on `core/libs/prx/libSceSulphaDrv/CMakeLists.txt:1-15`.
     - Each call returns 0, whatever its arguments.
   - **Size:** 12 rows. It could merge with another library's silent-stub batch.

## Caveats

- Orbital's prototypes are the PS4 ones. That the PS5 module, with the same NIDs, keeps them is assumed, not measured.
- `SceContentSearchContentId` and `SceContentSearchMimeType` are not declared in the tree.
- A silent stub accepts every input. The console's errors (no init, bad content id, menu already open) stay unmodelled.
