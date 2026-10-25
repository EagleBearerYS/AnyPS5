# libSceAudioPropagation (9 todo: B-S 2, C-silent 2, D 5)

**What it is:** geometric audio propagation: a system in caller memory, rooms, portals, materials and sources, ray exchange, per-source audio paths and an ambisonic/speaker render. Also lock, query and diagnostic-report calls.

**Existing state:**
- 24 done in `core/libs/prx/libSceAudioPropagation/Export.cpp`. There is no propagation (debt `docs/dev/debt/libSceAudioPropagation.md:5`):
  - attributes are validated and dropped;
  - sources own 36 invalid path slots;
  - Render keeps the destination mix and clears `channelCount`.
- State:
  - `SystemState` lives in the caller's CPU memory, with a magic, `frameSamples` and `maxSources` (`Export.cpp:26-30`).
  - A process-wide `Registry`, behind one mutex, holds `systems`, `objects`, `orphans`, `sourcePaths` and `paths` (`:44-52`).
  - Handle checks: `liveSystem`, `requireSystem` and `requireObject` (`:74-87`). An invalid argument throws `std::invalid_argument` through `require` (`:59`).
- Error constants: `ErrorInvalidArgument` 0x8A700001, `ErrorInvalidHandle` 0x8A700002 and `ErrorNullPointer` 0x8A700003 (`AudioPropagation.hpp:13-15`), plus the literal 0x8A700007 for a bad descriptor (`Export.cpp:138`).
- The todo declarations are in `include/Api.hpp:8-16`. Their records are recovered:
  - `SourceInfo`, 80 bytes (`AudioPropagation.hpp:51-62`);
  - `SystemInfo`, 48 bytes (`:64-75`);
  - `ReportInfo`, 72 bytes, with a SysV `va_list` (`:96-113`);
  - `ReportHandler` (`:115`).
- Tests: `guest_audio_propagation_tests` (`core/libs/tests/GuestAudioPropagation.cpp`), registered in `core/libs/prx/libSceAudioPropagation/CMakeLists.txt` at `:6` (`add_test_executable`) and `:10` (`add_test`). Today it asserts that `sceAudioPropagationSystemLock(0)` throws `std::runtime_error` (`GuestAudioPropagation.cpp:386`).

**Counts:** A 0, B-S 2, B-M 0, B-L 0, C-real 0, C-silent 2, D 5.

## Families

- **B-S (2): system lock.** `SystemLock` and `SystemUnlock` give the title's threads a per-system lock.
  - Validation is `requireSystem` (`Export.cpp:79`), like every other system call.
  - The shape is the done `sceNgs2SystemLock`/`Unlock` (`core/libs/prx/libSceNgs2.native/src/System.cpp:388`, `:403`), whose console behaviour is recorded in `docs/dev/debt/libSceNgs2.native.md:35`.
  - Nothing is measured for this library: nesting, unlock from another thread or without a hold, and whether `SourceRender` waits for the lock or only tries it, as NGS2's Render does. These cases throw.
- **C-silent (2): report handler registry.** `ReportRegisterHandler` and `ReportUnregisterHandler` keep (category, handler, userData) under an opaque registration, as the done `sceNgs2ReportRegisterHandler`/`Unregister` do (`System.cpp:465`, `:475`; debt `libSceNgs2.native.md:34`).
  - The handler is never called: invalid input throws instead of reporting, and `ReportApi` is D.
  - Silent stub, needs a title.
- **D (5):**
  - `ReportApi`: only the signature and the `ReportInfo` layout are recovered. How a category selects handlers, the call order and the int result are unknown.
  - `SystemQueryInfo`: `index` and the final word have no semantics (debt `:11`). `pathCapacityPerSource` could be 6 or 36: QueryMemory sizes 6 or 36 paths per source (`Export.cpp:150`), but every source gets `MaxPaths` 36 slots (`AudioPropagation.hpp:132`). Size validation is unknown.
  - `SourceQueryInfo`: attributes 2/3, the constant-one field and the fourth position/direction words have no semantics (debt `:11`). The tree stores no source position or direction (`Export.cpp:310-316`).
  - `SystemEnumHandles`: the return convention (count written or total), overflow and null behaviour are unrecorded. NGS2's analogue is itself a todo (`core/libs/prx/libSceNgs2/PendingExports.cpp:25`).
  - `ResetAttributes`: what a reset writes into an `Attribute` (`AudioPropagation.hpp:140-147`) is unknown, and no defaults are kept.

## Batch proposals

1. **"audiopropagation: system lock" (2):** `sceAudioPropagationSystemLock`, `sceAudioPropagationSystemUnlock`.
   - Files: `core/libs/prx/libSceAudioPropagation/Export.cpp`. Add a per-system lock record keyed by the system handle beside `Registry` (`:44-52`), and erase it in `SystemDestroy` (`:184`).
   - Semantics: `requireSystem` (`:79`), following the pattern of `sceNgs2SystemLock` (`core/libs/prx/libSceNgs2.native/src/System.cpp:388-409`). Throw for the following, and record them in `docs/dev/debt/libSceAudioPropagation.md`:
     - a nested lock;
     - an unlock without a hold or from another thread;
     - destroying a locked system.
   - Tests: extend `guest_audio_propagation_tests` (`core/libs/tests/GuestAudioPropagation.cpp`; `core/libs/prx/libSceAudioPropagation/CMakeLists.txt:6`, `:10`):
     - replace the `:386` runtime_error expectation;
     - Lock then Unlock on a live system returns 0;
     - a second thread's Lock blocks until Unlock;
     - an unknown or destroyed system throws `std::invalid_argument`;
     - the unmeasured cases throw.
   - Size: 2 functions. This is below the 10-30 range. Merge it with the C-silent registry below if a title needs that, or with another audio library's batch.
2. **"audiopropagation: report handler registry" (2, C-silent):** only when a title needs it. Model it on `sceNgs2ReportRegisterHandler` (`System.cpp:465-484`).
   - Tests in the same executable:
     - Register returns a non-null registration;
     - Unregister of it returns 0;
     - a second Unregister throws.

## Dependency notes

- None. No new subsystem or third-party code is needed.

## Caveats

- The input file's line numbers for this library were offset. All lines above were re-read from the tree: the todos are at `Export.cpp:428-471`.
- Debt `:11` says the query, lock, enumeration and report callback signatures are recovered. The semantics behind them, apart from the field layouts, are not in the tree, which is why `QueryInfo`, `EnumHandles` and `ReportApi` stay D.
