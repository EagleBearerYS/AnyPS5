# libSceNpSessionSignaling (8 todo: B-S 2, D 6)

**What it is:** PSN session signaling, which sets up peer-to-peer connections between members of a player session. It provides contexts, session and user activation, connection and group lookups, local network info, memory and connection statistics, and custom properties.

**Existing state:**
- 18 done, all in `core/libs/prx/libSceNpSessionSignaling/Export.cpp`.
- The model is offline: contexts exist, but sessions never activate.
  - Activation returns UNAVAILABLE (`:99-106`, `:142-148`).
  - Every lookup on a valid context ends at not-found (`:134-140`, `:203-252`).
  - Debt `docs/dev/debt/libSceNpSessionSignaling.md:7` and `:12` record this.
- State: `g_mutex`, `g_initialized` and `g_contexts` (10 slots) at `:25-27`, with `FindContext` (`:62`) and `CreateContext` (`:68`).
  - `sceNpSessionSignalingInitialize` (`:88`) validates `poolSize` (`:91`) but does not keep it.
- Error constants (`:10-17`):
  - INVALID_ARGUMENT 0x80553303
  - CONN_NOT_FOUND 0x8055330C
  - UNAVAILABLE 0x80552D06
  - NOT_INITIALIZED 0x80553301
  - ALREADY_INITIALIZED 0x80553302
  - CONTEXT_LIMIT 0x80553307
  - CONTEXT_NOT_FOUND 0x80553308
  - GROUP_NOT_FOUND 0x8055330A
- Records (`include/Api.hpp`): `SessionSignalingMemoryInfo {poolSize, usedSize, peakUsedSize}` (`:17`) and `SessionSignalingConnectionStatistics {peakConnectionCount, connectionCount, connectingCount, connectedCount}` (`:73`). Their sizes are static-asserted in `Export.cpp:29`, `:35-36` and `:55-58`.
- Check order: debt `:12` gives the PS5 12.00 module's order from disassembly: 0x80553301 before initialization, 0x80553303 for a null pointer, then 0x80553308 for an unknown context.
- Tests: `guest_np_session_signaling_tests` (`core/libs/tests/GuestNpSessionSignaling.cpp`), registered at `core/libs/prx/libSceNpSessionSignaling/CMakeLists.txt:6`, with `add_test` at `:9`.

**Counts:** A 0, B-S 2, B-M 0, B-L 0, C-real 0, C-silent 0, D 6.

## Families

- **B-S (2), offline statistics:**
  - `sceNpSessionSignalingGetConnectionStatistics`: no connection ever exists here, so all four counts are 0 after the initialization and null checks.
  - `sceNpSessionSignalingGetMemoryInfo`: report the pool size given to `Initialize`, with zero use and zero peak, as `sceNpWebApi2GetMemoryPoolStats` already does (`core/libs/prx/libSceNpWebApi2/Export.cpp:111-119`). Its zero usage is recorded in `docs/dev/debt/libSceNpWebApi2.md:15`.
  - Both declarations already carry typed signatures and asserted layouts. Neither row is in the unknown-signature debt line (`:8`).
- **D (6), unknown signature:** CreateJoinActivate, GetCustomProperty, GetGroupFromPeerAddress, InitializeWithPort, JoinActivate and SetCustomProperty.
  - All six are parameterless declarations with no credible prototype (debt `:8`; `Export.cpp:239`, `:254`, `:259`, `:264`, `:269`, `:274`).
  - The two activate calls would end in UNAVAILABLE in this model, but without the argument list even that cannot be returned safely.

## Batch proposals

1. **"np-session-signaling: offline memory info and connection statistics" (2):** `sceNpSessionSignalingGetMemoryInfo` and `sceNpSessionSignalingGetConnectionStatistics`.
   - Files:
     - `core/libs/prx/libSceNpSessionSignaling/Export.cpp`: keep `param->poolSize` in a new static next to `g_initialized` and clear it in `Terminate`; implement both calls under `g_mutex`;
     - `docs/dev/debt/libSceNpSessionSignaling.md`: record the zero usage and the borrowed check order.
   - Semantics:
     - the library's own check order (debt `:12`) and the offline model (debt `:7`);
     - the `libSceNpWebApi2` pool-stats neighbour (`core/libs/prx/libSceNpWebApi2/Export.cpp:111-119`).
   - Tests, extending `guest_np_session_signaling_tests` (`core/libs/tests/GuestNpSessionSignaling.cpp`, `CMakeLists.txt:6`, `:9`):
     - both return 0x80553301 before `Initialize` and after `Terminate`;
     - both return 0x80553303 for a null pointer after `Initialize`;
     - after `Initialize` with pool size N, `GetMemoryInfo` gives `{N, 0, 0}`;
     - after creating contexts and a failed `ActivateUser`, `GetConnectionStatistics` gives four zeros.
   - Unmeasured on a console:
     - whether these two calls follow the same check order;
     - whether `poolSize` echoes the requested size or the size after the library's internal rounding;
     - real pool usage.
   - Smaller than 10. It could merge with other offline NP statistics rows (for example libSceNpSignaling's or libSceNpWebApi's) into one "NP offline statistics" task.

## Dependency notes

- None. The six D rows need prototypes from title call sites. Once those exist, the activate variants fit the existing UNAVAILABLE model, and the custom-property pair needs a context lookup and per-context storage.
