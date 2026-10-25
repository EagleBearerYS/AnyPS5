# libSceSystemLogger2Delivery (4 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 4)

**What it is:** the delivery side of the SystemLogger2 telemetry service: batch delivery events and the results of deliveries.

**Existing state:**
- Nothing is done. `core/libs/prx/libSceSystemLogger2Delivery/Export.cpp:9-27` holds four parameterless declarations, each of which throws.
- There is no `CMakeLists.txt`, so `Export.cpp` is built by `core/libs/CMakeLists.txt:159`.
- Debt: `docs/dev/debt/libSceSystemLogger2Delivery.md:5` says no credible prototypes were found.
- Tests: none registered.
- Neighbours: libSceSystemLogger, libSceSystemLogger2 and libSceSystemLogger2NativeQueueClient are all still todo, so no done call gives a state or an answer.

## Families

- **D (4): GetBatchResult, GetResult, SetBatchDeliveryEvent and Terminate.**
  - This is private system-service IPC whose records, event object and results are unknown.
  - Terminate has no exported Initialize to pair with, so the state it ends is unknown too.

## Batch proposal

None. No PR claims these rows.

## Caveats

- If a title calls one of these, its telemetry delivery should simply have nowhere to go offline. That answer is unmeasured, so a constant would be a C-silent stub needing that title.
