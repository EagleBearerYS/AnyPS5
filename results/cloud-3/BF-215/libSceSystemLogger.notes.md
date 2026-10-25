# libSceSystemLogger (3 todo: D 3)

**What it is:** the client of the system telemetry logger (initialize, set log data, terminate).

**Existing state:**
- 0 done. `core/libs/prx/libSceSystemLogger/Export.cpp:9-22` holds three parameterless declarations.
- No state, no CMakeLists.txt and no guest test.
- Debt `docs/dev/debt/libSceSystemLogger.md:5`: no credible prototypes. Orbital `db_types.json@b321fcdb` has no entry for them.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 3.

## Families

- **D (3): unknown signature, for a telemetry service the tree does not model.**
  - `Initialize` may return or write a handle.
  - `SetLogData`'s record format is unknown.
  - `Terminate`'s arguments and errors are unknown.

## Batch proposals

None.

## Caveats

- Once a prototype is known, `SetLogData` is a C-silent candidate: logged data is never observable in the tree.
- `Initialize` and `Terminate` would follow only if they have no output.
- The sibling libraries `libSceSystemLogger2` (`core/libs/prx/libSceSystemLogger2/Export.cpp:9-32`) and `libSceSystemLogger2NativeQueueClient` are also all throws.
