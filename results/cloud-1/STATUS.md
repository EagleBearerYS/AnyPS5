BUSY PRM-029
RESULT:
- Working on PRM-029 (high), claimed 2026-10-16T22:48:08Z: CI Linux mirror on #6012 palette fix 84cae0bda0.
- Last finished: PRM-028 (high), results/cloud-1/PRM-028.md.
  - Branch `pm/6636-snapshot-writes` at 6e5b5bdf4, on #6636's head `89ab0ea75`: `CompleteSnapshotWrites` before `SameSnapshots`, plus a draw case in `AddressDrawReuse`.
  - Red without the sync: "(1 reuses, 0 builds)"; green with it. ×20 passed.
  - `^agc` 607/607 (one earlier run hit the known `preparation_scope_settle` flake; it passes 5/5 alone).
- Before that: BF-040 (`perf/agc-reuse-address-dispatch-builds` 1660019df).
