# libSceRudp (8 todo: A 0, B-S 1, B-M 0, B-L 0, C-real 0, C-silent 0, D 7)

**What it is:** reliable UDP over a title's own sockets: contexts, bind, initiate and activate, read and write, per-context options, poll sets and an event pump.

**Existing state** (`core/libs/prx/libSceRudp/Export.cpp`, built with libkernel's `TimedWait.cpp` at `core/libs/prx/libSceRudp/CMakeLists.txt:1-4`):
- 26 done. The model is offline: contexts can be created, bound and configured, but no connection is ever established.
  - Initiate answers CONN_REFUSED (`:270`).
  - Write and Read answer CONN_RESET (`:342`, `:354`).
  - GetSizeWritable and GetSizeReadable answer NOT_ACCEPTABLE (`:317`, `:331`).
- State: `g_inited` and `g_ctx` with a per-context option map (`:61-85`), and polls (`:89-96`).
- Errors (`:19-29`): NOT_INITIALIZED 0x80770001 through TOO_MANY_CONTEXTS 0x80770020, including NOT_ACCEPTABLE 0x80770011 (`:27`).
- Measured on a PS5:
  - GetStatus (debt `:11`);
  - GetMaxSegmentSize gives 1410 after init and NOT_INITIALIZED before it (debt `:12`, `:372-382`, asserted by `core/libs/tests/GuestRudpPump.cpp:26-46`).
- Sources the debt already uses:
  - options follow Vita3K at c86ac732 (debt `:13`);
  - polls follow Vita3K and RPCS3 (debt `:14`).
- Debt `:15` lists Accept, Flush, GetLocalInfo, Listen, PollCancel and SetMaxSegmentSize as having no credible prototypes.
- Tests: the input lists none, but five are registered in `core/libs/CMakeLists.txt`:
  - `guest_rudp_status` (`:643`, `:647`);
  - `guest_rudp_context_queries` (`:653`, `:657`);
  - `guest_rudp_pump` (`:663`, `:667`);
  - `guest_rudp_context_lifecycle` (`:673`, `:677`);
  - `guest_rudp_poll` (`:683`, `:687`).
- Sources read for this pass:
  - [Vita3K `SceLibRudp.cpp`](https://github.com/AndnixSH/Vita3K-PS-Vita-OS-Fork/blob/c86ac732bc811057e690bed280c5076bdf95385e/vita3k/modules/SceRudp/SceLibRudp.cpp) at c86ac732;
  - [RPCS3 `cellRudp.cpp`](https://github.com/RPCS3/rpcs3/blob/8b9ee60569753db8406b66d135cce112156172a1/rpcs3/Emu/Cell/Modules/cellRudp.cpp) at 8b9ee605.

## Families

- **B-S (1): `sceRudpSetMaxSegmentSize` (`:498`).**
  - Prototype `(uint16_t mss)`: Vita3K `#L1573-L1584` and RPCS3 `#L149-L153` agree.
  - Semantics (Vita3K): NOT_INITIALIZED before init, NOT_ACCEPTABLE while any context exists, otherwise store the value.
  - Vita3K's default of 1410 (`#L318`, `#L370`) matches the PS5 measurement (debt `:12`), which supports reading the PS5 library as the same design.
- **D (7):**
  - `sceRudpAccept` and `sceRudpListen` have no prototype. Vita3K has neither, and RPCS3 has argument-less unimplemented stubs (`#L203-L207`, `#L215-L219`).
  - `sceRudpFlush`, `sceRudpGetLocalInfo` and `sceRudpPollCancel` are unimplemented and argument-less in both sources (Vita3K `#L1052-L1054`, `#L1076-L1078`, `#L1337-L1339`; RPCS3 `#L263`, `#L191`, `#L293`).
    - What a cancelled `sceRudpPollWait` (`:438-456`) returns is unknown.
  - `sceRudpGetRemoteInfo` (`:287`): its signature was recovered in 8d3314c824, but no context ever connects, and the answer for an unconnected context is unknown. Both sources leave it unimplemented.
  - `sceRudpGetOption` (`:241`): done for 16 of 19 options. It still throws for options 12 and 13, whose meanings are unknown, and for last-error option 14, which is not maintained (`:250-253`, debt `:13`).
    - In Vita3K the last error comes from connection events (`#L499`, `#L553`) that never happen here.
    - The console's value after a refused Initiate is unmeasured.

## Batch proposal

**"feat(libSceRudp): settable maximum segment size" (1 row).**
- Implementation:
  - a segment size under `g_mutex`, initialised to `RUDP_MAX_SEGMENT_SIZE` (`:40`);
  - `sceRudpSetMaxSegmentSize(uint16_t)` answers NOT_INITIALIZED before init, NOT_ACCEPTABLE while `g_ctx` is not empty, and 0 after storing the value;
  - `sceRudpGetMaxSegmentSize` (`:372-382`) reports the stored value.
- Tests: extend `guest_rudp_pump_tests` (`core/libs/tests/GuestRudpPump.cpp`, `core/libs/CMakeLists.txt:663`, `:667`):
  - Set before init gives 0x80770001;
  - after init, Set(1200) gives 0 and Get gives 1200;
  - after `sceRudpCreateContext`, Set gives 0x80770011 and Get is unchanged;
  - after `sceRudpTerminate` of that context, Set succeeds again.
- Debt: remove the name from `:15`, and correct `:12`, which says "there is no `sceRudpSetMaxSegmentSize`", so 1410 is always reported. That line is stale: the export exists at `:498` and is listed at `:15`.
- No claiming PR.

## Caveats

- Vita3K reimplements the PS Vita library; it is not a PS5 measurement. Vita3K checks no range for `mss` (0, or above 1410). Whether `sceRudpEnd` resets the value is unknown: Vita3K's End (`#L1032-L1047`) keeps it. The test should not assert either.
- `sceRudpGetRemoteInfo` throws with no debt entry: 8d3314c824 removed its "unknown signature" line and added none.
- `Export.cpp` keeps two code comments (`:15-16`, `:282`), against the no-comments rule.
