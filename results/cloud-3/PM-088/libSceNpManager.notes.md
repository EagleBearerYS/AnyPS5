# libSceNpManager (156 todo: B-S 6, C 37, D 113)

What it is: the NP account and sign-in manager (account queries, state and reachability callbacks, NP requests), plus internal families exported from the same module: `sceNpManagerInt*` (IPC layer, `*Sdk` forms for SDK modules), `sceNpAsm*` (ASM client: service URLs, title tokens), `sceNpIdMapper*` and `sceNpInGameMessage*`.

Offline model in the tree (`Export.cpp:11`): PSN is not emulated, the user is signed out and has never signed up (`sceNpHasSignedUp` false, `sceNpGetState` writes 1). Errors used: `SCE_NP_ERROR_INVALID_ARGUMENT` 0x80550003 for null outputs, `SIGNED_OUT` 0x80550006 for user-id forms, `USER_NOT_FOUND` 0x80550007 for online-id account queries (no local user owns the id), `REQUEST_NOT_FOUND` 0x80550014. libSceNpAuth returns `SIGNED_OUT` for authorization codes and ID tokens.

Classes:
- C (37), all real behaviour, no silent stubs:
  - Public A forms (CheckNpAvailabilityA, GetAccountDateOfBirthA, GetAccountLanguageA, GetParentalControlInfoA, GetGamePresenceStatusA): `SIGNED_OUT` with null checks, as `sceNpGetAccountCountryA`. GetGamePresenceStatus (online id): `USER_NOT_FOUND`.
  - SetContentRestriction (0), CheckCallbackForLib (0, nothing can be registered), IdMapper mappings (`SIGNED_OUT`).
  - Int/Sdk twins only where every public twin returns the same `SIGNED_OUT` (account id, NpId, online id, user-id lookups, age, CountryA, LanguageA) and where NpAuth does (auth code, ID token); user-scoped tokens and tickets. These read no argument, as `sceNpGetAccountIdA` already does, since the Int argument lists are unverified.
- B-S (6): IdMapper and ManagerInt Create/Delete/AbortRequest over the existing `AddRequest`/`liveRequests` (`Export.cpp:32`).
- D (113):
  - ASM client (48).
  - Int state/flag getters that write an output (23).
  - Int forms whose twins disagree by argument form (5).
  - Login flow (7), clears (5), callback registrations (6), InGameMessage (5): its Initialize/CreateHandle are not exported in the tree.
  - Debt-listed unknowns: SandboxInfo, NotifyPlusFeature, the two unnamed NIDs.
  - IsPlusMember, whose return type may be a bool.
  - Other Int queries (9): availability, VSH and client-credential tokens, title ID/secret, app type, presence. Their argument lists or offline errors are unknown.

Third-party: none.

Batch proposals:
1. "np: signed-out A forms and ID mapper" (15): the 5 public A forms, GetGamePresenceStatus, SetContentRestriction, CheckCallbackForLib and the 7 IdMapper calls. Test: extend `guest_np_manager_tests` (`core/libs/tests/GuestNpManager.cpp`, `libSceNpManager/CMakeLists.txt:21`):
   - null output → 0x80550003, otherwise 0x80550006/0x80550007 with the output untouched;
   - an IdMapper request lifecycle: create, then delete, then delete again;
   - CheckCallbackForLib returns 0.
2. "np: NpManagerInt signed-out twins" (28): the 25 Int C rows and the 3 Int request calls. Test: the same executable asserts 0x80550006 for each and that request ids come from the shared pool. First confirm on a title that ships an SDK module importing the `*Sdk` forms that it treats the error as signed out.
