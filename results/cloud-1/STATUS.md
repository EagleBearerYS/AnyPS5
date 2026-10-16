BUSY BF-037
RESULT:
- Working on BF-037 (high), claimed 2026-10-16T18:50:14Z: the ceiling of a persistent device-wide descriptor heap against per-draw descriptor sets, on lavapipe. Throwaway branch `exp/descriptor-heap-ceiling`.
- Last finished: BF-038, results/cloud-1/BF-038.md.
  - Host write hook renamed to `GuestArenaSetHostWriteHook_nid_no_patch` / `PrepareHostWrite`: `fix/agc-host-write-pending-storage` 082c3268a.
  - The merges with #6234 (`test/host-write-x-6234` 8b6b9d7dd) and #6184 (`test/host-write-x-6184` 15fd13d74) are clean: both hooks kept, and all the named tests pass ×10.
- Before that: BF-035.
