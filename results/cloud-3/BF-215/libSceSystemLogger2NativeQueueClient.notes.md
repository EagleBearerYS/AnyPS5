# libSceSystemLogger2NativeQueueClient (5 todo: D 5)

**What it is:** the client of the system logger's native delivery queue. It checks and masks data, gets events, switches delivery on and off, reports delivery results and configures the queue.

**Existing state:**
- 0 done. `core/libs/prx/libSceSystemLogger2NativeQueueClient/Export.cpp:9-32` holds five parameterless declarations.
- No state, no CMakeLists.txt and no guest test.
- Debt `docs/dev/debt/libSceSystemLogger2NativeQueueClient.md:5`: no credible prototypes. Orbital `db_types.json@b321fcdb` has no entry for them.
- The service it talks to is not modelled. `libSceSystemLogger2` itself is all throws (`core/libs/prx/libSceSystemLogger2/Export.cpp:9-32`).

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 5.

## Families

- **D (5): unknown signature.** The layouts of the data and mask, the event, the delivery result and the queue configuration are unknown. So is the argument of `SetDeliveryEnable`.

## Batch proposals

None.

## Caveats

A game title is not expected to import this library, and the tree records no title that does.
