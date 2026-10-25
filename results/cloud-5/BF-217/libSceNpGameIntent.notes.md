# libSceNpGameIntent (8 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 8)

**What it is:** game intents, which are activity launch and join requests delivered to the title. The module also holds the NP app launcher and three NP session-service queries.

**Existing state** (`core/libs/prx/libSceNpGameIntent/Export.cpp`; there is no CMakeLists, so `core/libs/CMakeLists.txt:159` builds `Export.cpp`):
- 10 done:
  - Initialize, Terminate, their Vsh forms, and AppLauncher Initialize and Terminate return 0 (`:29-32`, `:46-48`, `:69-80`, `:87-89`). These are silent stubs (debt `:5-6`).
  - InitParamInit and InfoInit fill fpPS4's layouts (`:50-67`; debt `:11-12`).
  - ReceiveIntent reports no intent: user -1, zeroed fields and INTENT_NOT_FOUND (`:34-44`).
  - GetPropertyValueString returns VALUE_NOT_FOUND (`:20-27`).
- Errors (`:11-13`): INVALID_ARGUMENT 0x80553804, INTENT_NOT_FOUND 0x80553806, VALUE_NOT_FOUND 0x80553807. The library has no session-service error.
- Debt (`docs/dev/debt/libSceNpGameIntent.md:10`):
  - the eight todo names are from the PS5 3.20 export inventory;
  - only `sceNpAppLauncherLaunchApp` has a partial prototype from a title's call sites, `(user_id, uri struct, context)` returning int, and the debt says no launcher semantics are derivable;
  - the rest are parameterless and make no ABI claim.
- No tests.

## Families

- **Launch (4): D.** AppLauncherLaunchApp, GameIntentLaunchApp, LaunchApp2 and NotifyAppLaunched.
  - Launching or announcing another application has no console answer in the tree.
  - LaunchApp's uri struct is unresolved.
- **UDS communication id (1): D.** For GetUdsNpComId the output form is unknown.
- **Session service (3): D.** GetJoinedPartySession, GetRunningGamePlayerSession and SendPlayerSessionInvitations.
  - No prototype.
  - The no-PSN answer is unmeasured.
  - No other library implements these names.

## Batch proposal

None.
- Claim: PR 10573 (open) changes `sceNpAppLauncherLaunchApp`. It must bring the launcher semantics or keep the throw.

## Caveats

- The session-service rows could become C-real if a done NpSessionService-style neighbour or a measured no-PSN error appears. Neither exists in the tree today.
