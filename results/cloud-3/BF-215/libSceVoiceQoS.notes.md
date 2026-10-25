# libSceVoiceQoS (11 todo: D 11)

**What it is:** voice-chat quality of service on top of libSceVoice: local and remote endpoints, connections, packet read and write, endpoint and connection attributes, status, and chat enable and disable.

**Existing state:**
- 14 done.
- One state object, `QoS`: the `initialized` flag, `endpointLimit` (7 or 15), and maps of local endpoints, remote endpoints and connections (`core/libs/prx/libSceVoiceQoS/Export.cpp:29-36`). Access goes through `Locked` (`:61`).
- Error constants (`:17-20`): `NOT_INIT` 0x804E0901, `ARGUMENT_INVALID` 0x804E0902, `LIMIT` 0x804E0907 and `INITIALIZED` 0x804E0905.
- `Init` accepts only 0x40000 bytes and app types 0x20000000 and 0x10000000 (`:14-16`, `:76`).
- Silent stubs, each needed by PPSA03393 (debt `docs/dev/debt/libSceVoiceQoS.md:5-6`):
  - `ReadPacket` reports no packet;
  - `WritePacket` accepts everything.
- The endpoint attribute setters validate the recovered selectors, then throw (`:158`, `:165`; debt `:16`).
- Voice ports are never created in libSceVoice (debt `:14`).
- Test: `guest_voice_qos_tests` (`core/libs/tests/GuestVoiceQoS.cpp`), registered at `core/libs/prx/libSceVoiceQoS/CMakeLists.txt:6`, and run twice (`:10`, `:11`) for the two app types.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 11.

## Families

- **D (4), attribute getters and setters:** `GetLocalEndpointAttribute`, `GetRemoteEndpointAttribute`, `GetConnectionAttribute` and `SetConnectionAttribute`.
  - Endpoint selectors are recovered only for the setters (debt `:16`), and those setters throw on valid requests.
  - The values would come from voice ports that are never created (debt `:14`).
  - No connection selectors are recovered at all.
- **D (2), status:** `GetStatus` and `DebugGetStatus`.
  - Status ids and values are not recovered.
  - No packet statistics exist, since streaming is a silent stub (debt `:5-6`).
- **D (1), `InitHQ`:** the HQ mode's memory size and app types are not recovered. Only `Init`'s are confirmed (debt `:14`).
- **D (4), unknown signatures:**
  - `EnableChat` and `DisableChat` were declared without parameters from PPSA14632's imports (`docs/dev/TechnicalDebt.md:23`). libSceVoice's own `EnableChat` and `DisableChat` (`core/libs/prx/libSceVoice/Export.cpp:384`, `:424`) throw once initialized.
  - `SetMode` and `Terminate` are absent from the supplied module (debt `:10`).

## Batch proposals

None. No row is A, B or C-real.

## Dependency notes

- Most rows need the libSceVoice port model that debt `:14` says is missing: port creation, volume and mute, and packet accounting. They also need the remaining selectors and status ids, recovered from the module, as the setters' were.
