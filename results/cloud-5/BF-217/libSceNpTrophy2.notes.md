# libSceNpTrophy2 (23 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 23)

**What it is:** PS5 trophies: contexts and handles, game, group and trophy information and icons, the unlock callback and the trophy-list UI. All 23 todo rows are the `sceNpTrophy2System*` family: shell-side calls for trophy sets, configuration, icon URIs, sync, debug and data removal.

**Existing state:**
- **`Export.cpp` is not built.** `core/libs/prx/libSceNpTrophy2/CMakeLists.txt:1-10` builds the eight `src/` files only, so none of the 23 System rows is exported (`Export.cpp:9-122`). They are declared in `include/Api.hpp:8-30`.
- 18 done, in `src/`:
  - `Context.cpp`: CreateContext `:56`, DestroyContext `:65`, RegisterContext `:70`, UnregisterUnlockCallback `:77`, GetRewardIcon `:81`, ShowTrophyList `:92`;
  - `Handle.cpp`: CreateHandle `:10`, DestroyHandle `:18`, AbortHandle `:23`;
  - `GameInfo.cpp`, `GroupInfo.cpp` and `TrophyInfo.cpp`.
- State:
  - trophy data comes from the title's `sce_sys/trophy2/trophyNN.ucp` (`src/TrophyPackage.cpp`);
  - unlocks come from the `_trophy/<user id>_<service label>.txt` store (`src/TrophyStore.cpp`);
  - details are in debt `:16-20`.
- Errors: INVALID_TROPHY_ID 0x8055390A and ICON_FILE_NOT_FOUND 0x80553911 (`include/NpTrophy2Constants.hpp:7-8`).
- Debt (`docs/dev/debt/libSceNpTrophy2.md`):
  - `:12`: the 23 System exports were identified in the 3.20 export table and cross-checked on 13.60. Their signatures and behaviour are unverified, and they use the unknown-signature convention.
  - `:10`: even the app-side record layouts rest on KytyPS5 alone.
- Tests:
  - `guest_nptrophy2`: executable at `CMakeLists.txt:19`, registered at `:23`;
  - `np_trophy2_store`: `:30`, `:36`;
  - `np_trophy2_package`: `:43`, `:49`.
  - None calls a System row.

## Families (all D, all not exported)

- **Handle and context (4):** AbortHandle, CreateContext, CreateHandle and DestroyContext.
  - App-side twins are done (`Handle.cpp:10`, `:23`; `Context.cpp:56`, `:65`).
  - No source shows the System forms take the same arguments, so a forward would assume the shapes.
- **Icon URIs (4):** BuildGroupIconUri, BuildRewardIconUri, BuildTrophyIconUri and GetTrpIconByUri.
  - The URI format and the output contracts are not public.
  - No icon is read at all (debt `:16`).
- **Detail and configuration queries (5):** GetTrophyDetails, GetTrophyGroupConf, GetTrophySetArray, GetTrophyTitleConf and GetTrophyTitleDetails. The records are unknown.
- **Callbacks and sync (5):** CheckCallback, CheckNetSyncTitles, IsServerAvailable, RegisterTitleSyncedCallback and RegisterTitleUpdateCallback.
  - The callback signatures are unknown.
  - The no-PSN answer is unmeasured.
- **Debug and removal (5):** DebugLockTrophy, DebugUnlockTrophy, RemoveAll, RemoveTitleData and RemoveUserData. These are destructive store operations whose arguments are unknown.

## Batch proposal

None. No claims.

## Caveats

- **Unbuilt `Export.cpp`.** A title importing a System name fails at import resolution instead of throwing a named NotImplemented. Any future System implementation must add its source to `CMakeLists.txt:1-10`.
