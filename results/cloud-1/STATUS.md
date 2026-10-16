READY
RESULT:
- Last task: PRM-015 (high), done. Result: results/cloud-1/PRM-015.md. Branch: fork followup/6646-refused-heap-refresh 3bdb6274d9, four commits on review/6646-sparse-heap-mirror 356d2a9dfa.
- G2: a heap mirror whose range has left the write watch is planned again only when the host import path takes the range now. Otherwise it is kept and refreshed in full, every block counted as written, for sparse heaps too. The copy cap is never reached for it.
- G3: both Requires (refreshHeapMirrors, refreshHeapRuns) now refresh in full, collecting each mirror of a run on its own. #6546's whole-run refresh leaves the run's watched mirrors stale; the new run test shows it.
- Red on the base: agc_heap_mirror_unwatch and agc_sparse_heap_mirror_unwatch (both cache modes) exit 134 with "FATAL: an address-based build copies 2 MiB of 1 registered ranges, past 1 MiB". Green at the head.
- Checks:
  - CI configure, registration and build exit 0;
  - ^agc 612/612 on re-run; the first run lost only the known intermittent agc_driver_preparation_scope_settle, which then passed 20/20 alone;
  - the heap-mirror tests x10 passed 70/70;
  - conventions passed, with 0 added comment lines;
  - MinGW is clean for the test files.
- #6636: spaceMiss should return Unwatched only when the import path takes the range now. ResumeSpace then refreshes a refused mirror in full, with no rebuild.
