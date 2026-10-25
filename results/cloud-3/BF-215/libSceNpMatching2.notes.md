# libSceNpMatching2 (18 todo: D 18)

**What it is:** the NP Matching2 client for PSN matchmaking: library init and term, contexts, callback registration, then server, world, room, member, user-info, messaging and signaling requests to Sony's matching servers.

**Existing state:**
- 11 done. Offline only, following shadPS4 (debt `docs/dev/debt/libSceNpMatching2.md:5-6`).
- State: `matching2Mutex`, `matching2Initialized`, the `contexts` set and `nextContextId` (`core/libs/prx/libSceNpMatching2/Export.cpp:26-29`). `CheckContext` is at `:31`.
- Error constants (`:9-15`):
  - `ALREADY_INITIALIZED` 0x80550C02, `NOT_INITIALIZED` 0x80550C03 and `CONTEXT_MAX` 0x80550C04;
  - `CONTEXT_NOT_STARTED` 0x80550C08, `INVALID_ARGUMENT` 0x80550C0A and `INVALID_CONTEXT_ID` 0x80550C0B;
  - `SCE_NET_ERROR_RESOLVER_ETIMEDOUT` 0x804101E2.
- Offline answers:
  - `sceNpMatching2ContextStart` always answers `RESOLVER_ETIMEDOUT` (`:45`); shadPS4 says this was confirmed on a disconnected console.
  - No context ever starts, so `ContextStop` answers `CONTEXT_NOT_STARTED` (`:51`).
  - Callbacks are checked and never called (`:134-160`).
- All 18 todo rows are parameterless declarations (`include/Api.hpp`).
- Test: `guest_np_matching2_offline_tests` (`core/libs/tests/GuestNpMatching2Offline.cpp`), registered at `core/libs/prx/libSceNpMatching2/CMakeLists.txt:6` and `:10`.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 18.

## Families

- **D (17), server requests and room-local queries:**
  - The rows: `CreateJoinRoom`, `JoinRoom`, `LeaveRoom`, `KickoutRoomMember` and `SearchRoom`; `GetWorldInfoList`, `GetUserInfoList`, `SetUserInfo` and `GetRoomDataExternalList`; `GetRoomDataInternal`, `GetRoomMemberDataInternal`, `SetRoomDataExternal`, `SetRoomDataInternal` and `SetRoomMemberDataInternal`; `SendRoomMessage`, `SendRoomChatMessage` and `SignalingGetConnectionInfo`.
  - shadPS4 at 91e6afa, the file debt `:6` cites, gives each prototype (see the line in each tsv row). It answers them from its own matchmaking backend (`Mm*`, scheduled request-callback events).
  - The tree has no matching server.
  - What the console answers for a request on a context that never started is not in the tree. shadPS4 never checks the started state, and the tree has `CONTEXT_NOT_STARTED` only for `ContextStop`.
- **D (1), `GetServerId`:** shadPS4 writes server id 1 for its own server (L351). The console's answer offline is unknown.

## Batch proposals

None. No row is A, B or C-real.

## Caveats

- Unsure: one console measurement could turn the 17 request rows into C-real rows. Measure `sceNpMatching2CreateJoinRoom` (or any request) on a created but never-started context while offline.
  - If it returns `CONTEXT_NOT_STARTED` 0x80550C08, every request takes shadPS4's prototype.
  - Each request then runs, in shadPS4's order:
    - `NOT_INITIALIZED`;
    - `INVALID_ARGUMENT` for a null request or request id;
    - `INVALID_CONTEXT_ID`;
    - the measured answer.
  - Tests would go in `guest_np_matching2_offline_tests`.
  - Until then this is a guess, so the rows stay D.
