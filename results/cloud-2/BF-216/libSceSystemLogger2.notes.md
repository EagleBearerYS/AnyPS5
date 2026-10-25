# libSceSystemLogger2 (5 todo: A 0, B-S 0, B-M 0, B-L 0, C 0, D 5)

- **What it is:** going by the export names, the client of the system event logger (version 2), which submits telemetry events asynchronously. It has Initialize, Terminate, SetEventAsync, SetEventAsyncEx and GetTimestamp.
  - Two sibling libraries, libSceSystemLogger2Delivery and libSceSystemLogger2NativeQueueClient, cover delivery and the native queue. They are in the same state, with parameterless declarations and debt entries saying there is no prototype.
- **Existing state:** 0 done.
  - All 5 bodies in `core/libs/prx/libSceSystemLogger2/Export.cpp` (lines 9-32) are parameterless `NotImplemented_nid_no_patch` stubs. There is no state.
  - `docs/dev/debt/libSceSystemLogger2.md:5` lists all 5 as names from the PS5 3.20 export inventory with "no credible prototypes", and says the declarations are "not an ABI claim".
  - There is no CMakeLists.txt. The default branch of the prx glob builds the library (`core/libs/CMakeLists.txt:159`). No guest test exists.
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 5.
- **D families (all parameterless, no prototype, private system logging service):**
  - Event submission (2): SetEventAsync and SetEventAsyncEx. Dropping the event and returning 0 would be the natural PC answer, but whether the call writes a request ID or other output is unknown. A parameterless declaration cannot carry one, so it is not a C row.
  - Lifecycle (2): Initialize and Terminate.
  - Timestamp (1): GetTimestamp. Whether it returns or writes the value, and in what clock and unit, are unknown.
- **Batch proposal:** none (no A, B or C rows), and no guest test exists.
- **Caveats:** once a title shows SetEventAsync's prototype, it and Initialize and Terminate are good candidates for a silent stub that discards telemetry. Discarding telemetry does not affect the UI, though, so CONTRIBUTING's silent-stub rule would need a title and a maintainer decision.
