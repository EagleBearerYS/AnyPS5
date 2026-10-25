# libSceSulphaDrv (7 todo: D 7)

**What it is:** the Sulpha driver, a connection to a host-side tool. Its default configuration carries a buffer capacity and port 4001.

**Existing state:**
- 1 done: `sceSulphaGetDefaultConfig` (`core/libs/prx/libSceSulphaDrv/Export.cpp:21-26`).
  - A null pointer gives 0x804D0003.
  - Otherwise it writes the 24-byte record {size 24, capacity 0x400000, setting 3, port 4001}.
- Test: `guest_sulpha_config_tests` (`core/libs/tests/GuestSulphaConfig.cpp`; the record is checked at `:38`), registered at `core/libs/prx/libSceSulphaDrv/CMakeLists.txt:6` and `:10`.
- The seven todo rows are `(void)` declarations at `:11-51`.
  - `APS5_DUMMY_FUN` at `:10` only defines `DummyFunction_nid_no_patch` (`core/libs/prx/libc/include/General.hpp:98-102`); it is not a row.
- Debt `docs/dev/debt/libSceSulphaDrv.md:5`: the signatures and behaviour are unknown. Orbital `db_types.json@b321fcdb` has no `sceSulpha` entry.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 7.

## Families

- **D (7): unknown signature, and no Sulpha tool connection is modelled.**
  - Memory sizing and setup: `GetNeededMemory`, `Init`, `Shutdown`.
  - Client queries: `GetClientInfo`, `GetConnectedClientCount`.
  - Data calls: `Message`, `SetBookmark`.
  - How the needed size follows from the config, and what the calls answer with no tool connected, are not in the tree.

## Batch proposals

None.

## Caveats

- `GetConnectedClientCount` would be a C-real candidate ("no client connected") once its prototype is known, if the console answers it without `Init`.
- Extend `guest_sulpha_config_tests` when any of these rows gets a prototype.
