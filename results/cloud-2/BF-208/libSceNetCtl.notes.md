# libSceNetCtl (56 todo: A 10, B-S 3, C 5, D 38)

**What it is:** network-connection state for apps (state, info, result, NAT, interface statistics, callbacks). The same module also exports the internal `*IpcInt` client calls, the NpToolkit and ForLib callback forms, Wi-Fi scan and connect, bandwidth management, the NetBwe connection test and the console access-point (ApRp, ApCp, ApApp) calls.

**Existing state** (`core/libs/prx/libSceNetCtl/Export.cpp`):
- 29 done. 11 of them are ApRp constant-success calls (debt `:16`).
- Answers come from the host's interfaces, not from PSN (`:38`). `HostHasAddress` (`:234`) feeds the state, and `FindHostIpv4Interface` (`:434`) feeds GetInfo: address, netmask, gateway, MTU, DNS. The device is always reported as wired (`:547`).
- `g_initialized` starts true (`:89`, debt `:21`).
- Three 8-slot callback tables: `g_callbacks`, `g_callbacksV6` and `g_callbacksNpToolkit` (`:91-93`). They share `RegisterCallback`, `UnregisterCallback` and `CheckCallbacks` (`:95`, `:109`, `:507`). CheckCallbacks delivers an event only when the host address state changes.
- Error constants are at `:40-47`.
- Test: `guest_net_ctl_tests` (`core/libs/tests/GuestNetCtl.cpp`), built at `libSceNetCtl/CMakeLists.txt:6` and registered at `:10` (`add_test(NAME guest_net_ctl ...)`).

**Counts:** A 10, B-S 3, B-M 0, B-L 0, C 5 (all silent stubs), D 38.

## The two functions from title reports

`sceNetCtlGetNatInfo` and `sceNetCtlGetResult` are already counted **done**: neither calls `NotImplemented`. They are not in this table. Their `IpcInt` twins are.

- **sceNetCtlGetResult** (`Export.cpp:580-588`). What this PC answers:
  - null `result`: `INVALID_ADDR` 0x80412107 (`:582`);
  - events 1-3: writes 0 and returns 0 (`:586`);
  - any other event: throws `std::runtime_error` (`:584`).
  - The debt (`:13`) lists PPSA20052 (at boot) and PPSA24191 (event 1, no network). Both get 0 today.
  - The remaining gap is **D**:
    - the console's error code for an event outside 1-3 is not in the tree;
    - the nearest measured pattern is libSceNetCtlAp's GetResult, which checks init, then the pointer, then the event type, and answers `INVALID_TYPE` 0x80412990 in its own range (`core/libs/prx/libSceNetCtlAp/Export.cpp:92-96`, `docs/dev/debt/libSceNetCtlAp.md:13`);
    - whether a console writes a nonzero disconnect cause for event 1 is unconfirmed (debt `:13`).
  - A report that crashes here must name the event it passed.
  - Twin: `sceNetCtlGetResultIpcInt` is **A**, a forward to `:580`.
- **sceNetCtlGetNatInfo** (`Export.cpp:573-578`). What this PC answers:
  - null: 0x80412107 (`:575`);
  - `size != 16`: `INVALID_SIZE` 0x80412111 (`:576`);
  - every valid call: throws `std::logic_error` (`:577`, asserted by `GuestNetCtl.cpp:345-360`).
  - So any title that calls it correctly stops here.
  - Known: 16 bytes with a 32-bit size prefix. The other 12 bytes are unresolved (debt `:5`), so no NAT answer can be written without guessing.
  - Only the host-without-address case has a neighbour answer: `NOT_CONNECTED` 0x80412108. GetInfo (`:545`) and GetIfStat (`:534`) give it for the same absent network, so under the guide this branch would be C "real behaviour", though it is unmeasured for NatInfo.
  - With an address, the gap is **D**: the field layout plus a NAT-type answer. This PC has no Sony STUN service.
  - Lead, not checked here (no network): SharpProspero's `NetCtl.cs`, which the debt already cites for other NetCtl prototypes (`:12`, `:14`).
  - Twin: `sceNetCtlGetNatInfoIpcInt` is **D**: a 196-byte internal record with unresolved fields (debt `:9`), and its public twin throws anyway.

## Families

- **A (10): IpcInt twins of done calls.** Each forwards to its public twin, which has the same recovered argument registers (debt `:9`):
  - GetStateIpcInt and GetStateV6IpcInt → `:590`, `:597`;
  - GetResultIpcInt and GetResultV6IpcInt → `:580`, `:782`;
  - GetInfoIpcInt and GetInfoV6IpcInt → `:540`, `:617`. GetInfoV6 inherits its throw on a host with IPv6 (debt `:12`);
  - Register/UnregisterCallbackIpcInt → `:609`, `:627`;
  - Register/UnregisterCallbackV6IpcInt → `:613`, `:631`.
  - IpcInt registrations go into the public tables, because no `CheckCallbackIpcInt` is exported to dispatch a separate one.
- **B-S (3): ForLib callback table.** RegisterCallbackForLibIpcInt, UnregisterCallbackForLibIpcInt and CheckCallbackForLibIpcInt.
  - A fourth 8-slot table through the existing helpers, as the NpToolkit forms do (`:635`, `:649`, `:663`).
  - That design follows shadPS4 `net_ctl_obj.h` (debt `:15`), but shadPS4 has no ForLib form, so a separate table is an assumption.
- **C (5), all silent stubs; each needs a title:**
  - ClearEventIpcInt, ClearEventForNpToolkit and ClearEventForLibIpcInt return 0 for events 1-3 and throw otherwise. No per-event result is stored, since GetResult writes 0 for every event. The pattern is `sceNetCtlApClearEvent` (`libSceNetCtlAp/Export.cpp:62`).
  - Set/UnsetStunWithPaddingFlagIpcInt are `(void)` no-ops, because no STUN request is ever sent.
- **D (38):**
  - NetBwe connection test (9): service IPC, unknown event ids, 28-byte info.
  - Access point App/Cp/Restart/WPA key (7): the ApRp constant-success evidence does not cover them.
  - Connect, ConnectWithRetry, ConnectConf and Disconnect (4): system connection control; the host network must not be reconfigured; 1464-byte configuration.
  - Wi-Fi scan (6): 88-, 96- and 112-byte records, unresolved.
  - Bandwidth management (4): 32-byte info, unknown default.
  - GetState2IpcInt: no twin.
  - GetNatInfoIpcInt: see above.
  - EtherLinkMode, WifiType and WifiInfo: constants and 48-byte record not public; the host is reported as wired.
  - SetErrorNotificationEnabled, NetEvConfigInfo (incomplete type, `include/NetCtl.hpp:12`) and ConnectionElapsedTime (unit unknown; the host model keeps no connection time).

## Batch proposal

**"netctl: IpcInt twins and ForLib callback table" (13: the 10 A rows and the 3 B-S rows).**
- Semantics: the public twins above and the NpToolkit table pattern.
- Tests: extend `guest_net_ctl_tests` (`core/libs/tests/GuestNetCtl.cpp`, `libSceNetCtl/CMakeLists.txt:6`, `:10`):
  - every IpcInt getter returns what its twin returns:
    - null output gives 0x80412107;
    - GetInfoIpcInt codes 0 and 22 give 0x80412106;
    - GetResultIpcInt events 1-3 give 0 and write 0;
    - GetStateIpcInt matches `sceNetCtlGetState`;
    - GetInfoV6IpcInt(1) gives 0x80412108 when the V6 state is not 3;
  - a callback registered with RegisterCallbackIpcInt is delivered by `sceNetCtlCheckCallback`, as in `CheckCallbacks()` (`GuestNetCtl.cpp:94`);
  - UnregisterCallbackIpcInt(-1) returns 0x80412105;
  - ForLib table:
    - eight registrations succeed and the ninth returns 0x80412103;
    - unregistering a free id returns 0x80412104;
    - CheckCallbackForLibIpcInt delivers the current event only to ForLib callbacks.
- Record the IpcInt-twin and ForLib-table assumptions in `docs/dev/debt/libSceNetCtl.md`.

## Caveats

- The A rows assume an IpcInt export answers what its public twin answers. The evidence is identical recovered argument registers (debt `:9`), plus PM-088's treatment of the NpManagerInt twins. Nothing is measured.
  - The init check is unreachable either way (debt `:21`).
  - Null-pointer handling in the IpcInt wrappers is not recorded.
- The ForLib table may instead be the NpToolkit table, since "ForLib" may be what the NpToolkit forms call.
- The ClearEvent and STUN silent stubs need a title before they are written.
