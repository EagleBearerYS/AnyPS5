# libSceContentDelete (4 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 4)

**What it is:** deletes a user's media content (screenshots, videos) by content id or by path from the content database.

**Existing state** (`core/libs/prx/libSceContentDelete/Export.cpp`; no CMakeLists, built by `core/libs/CMakeLists.txt:159`):
- 4 done:
  - Initialize (`:20`): checks the 16 KiB heap (`:21`) and returns ALREADY_INITIALIZED on a second call;
  - Terminate (`:59`);
  - ByPath (`:27`) and ById (`:34`): answer NOT_INITIALIZED before init (`:28`, `:35`), and for every valid call after init they throw (`:31`, `:36`). The host content library is empty, and the console's error for missing content is unknown (debt `:5`).
- State: `g_initialized` only (`:11`).
- Errors: INVALID_ARGUMENT 0x809D5001, NOT_INITIALIZED 0x809D5002, ALREADY_INITIALIZED 0x809D5003 (`:13-15`).
- Debt `docs/dev/debt/libSceContentDelete.md`:
  - `:5` ById/ByPath;
  - `:6` Initialize: only the heap size field is resolved;
  - `:10` GlobalMstat, MemoryInitialize and MemoryTerminate: signatures unresolved.
- Test: `guest_content_delete_tests` (`core/libs/tests/GuestContentDelete.cpp`). It is registered in **another library's** CMake: built at `core/libs/prx/libSceContentExport/CMakeLists.txt:16` and registered at `:20`. The input lists no tests for this library.

## Families

- **ByIdListForSf (D):**
  - Declared `(const int64_t* contentIds, size_t count)` (`include/Api.hpp:10`). The typing came from commit 5536d61756, which also removed the debt line that named it. No source is recorded, and there is now no debt line.
  - Only the NOT_INITIALIZED branch is known, from its siblings. The main answer, for ids absent from the empty host library, is exactly what ById cannot answer either (`:36`).
- **GlobalMstat, MemoryInitialize, MemoryTerminate (3, D):** no signature (debt `:10`), no neighbour. How the memory calls relate to the 16 KiB heap is unknown.

## Batch proposal

None.

## Caveats

- ByIdListForSf could be brought to the same state as ById: NOT_INITIALIZED, then throw. Under the guide that counts as done. It would not change what an initialised title sees, so it is not a batch. If it is written, the list-argument source and a debt line must be added.
- ById and ByPath count as done, yet throw for every valid initialised call.
- No claim in the input. PR 10644 (open) names ByIdListForSf only in the debt file.
