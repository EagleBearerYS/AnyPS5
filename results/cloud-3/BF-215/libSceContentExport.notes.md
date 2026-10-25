# libSceContentExport (15 todo: B-S 1, D 14)

**What it is:** exports title-made files or data (screenshots, videos, Share Factory projects) into the system media library. It runs per-export sessions and edits exported content (favorites, rename, validate).

**Existing state:**
- 7 done, in `core/libs/prx/libSceContentExport/Export.cpp`.
- State: `g_initialized`, `g_sessionMutex` and `g_sessions` (`:18-20`). Session ids are the lowest free slot in 0-9 (`sceContentExportStart`, `:121-128`), and `RequireSession` (`:22-25`) throws for a bad id.
- Errors: `InvalidState` 0x809D3002, `Busy` 0x809D3003, `NotInitialized` 0x809D3004 and `ExecutionMax` 0x809D3018 (`:14-17`).
- `sceContentExportInit2` (`:30-37`) validates its `ContentExportInitParam2` and sets `g_initialized`.
- The host has no media library. The done `FromDataWithThumbnail` checks the session and then throws (`:142-146`), and `GetProgress` throws (`:130-134`) (`docs/dev/debt/libSceContentExport.md:5`).
- Recovered types are in `core/libs/SceTypes.hpp`:
  - `ContentExportInitParam` (`:1832-1840`, 24 bytes, asserted);
  - `ContentExportSfParam` (`:1842-1854`);
  - `ContentExportInitParam2` (`:1856-1863`);
  - `ContentExportParam` (`:1865-1870`).
- Tests: `guest_content_export_tests` (`core/libs/tests/GuestContentExport.cpp`), registered at `libSceContentExport/CMakeLists.txt:6` and `:10`. Line `:93` currently requires `sceContentExportInit(nullptr)` to throw `std::runtime_error`.

**Counts:** A 0, B-S 1, B-M 0, B-L 0, C-real 0, C-silent 0, D 14.

## Families

- **B-S (1):** `sceContentExportInit` (`:96`) is the legacy initializer over the recovered `ContentExportInitParam` (`mallocFunc`, `freeFunc`, `userData`). It sets the same state as `Init2` (`:30-37`) and throws for a null param, a missing allocator or a second init, as `Init2` does. The basis is in-tree only.
- **D (6): exports into a media library the host lacks.**
  - FromData, FromFile, FromFileWithThumbnail, FromFileForSf, FromFileWithContentIdList and FromFileWithTitleIdList.
  - The console's output path and progress behaviour are unknown, and the Share Factory metadata and option values are unresolved (debt `:11`).
  - The `FromDataWithThumbnail` pattern (`:142-146`, check the session, then throw) would only relabel these rows; it would not implement them.
- **D (4): content-id operations.** AddToFavorites, RemoveFromFavorites, RenameContent and ValidateContents act on exported content that never exists here. The console error for an unknown id is not in the tree.
- **D (4): unknown signature.** DataProvideFunction, Free, From and Malloc have no matching exports in the module listing (debt `:9`). They look like callback type names rather than callable exports.

## Batch proposals

1. **"contentexport: legacy sceContentExportInit" (1):** `sceContentExportInit`.
   - Files: `core/libs/prx/libSceContentExport/Export.cpp` and `docs/dev/debt/libSceContentExport.md`.
   - Semantics: `Init2` (`Export.cpp:30-37`) applied to the v1 layout (`SceTypes.hpp:1832-1840`).
   - Tests in `guest_content_export_tests` (`CMakeLists.txt:6`):
     - replace `:93` with: null param throws `invalid_argument`, and a missing `freeFunc` throws;
     - `Init` with two allocators returns 0, after which `Start` returns 0;
     - `Init` after `Init2` throws, `Term` returns 0, and `Init` works again.
   - This is a single row: merge it with `libSceBluetoothHid`'s initializer batch (2 rows).

## Dependency notes

- The 10 D rows tied to exporting need a new subsystem: a host-side media library (export directory, content ids, favorites and names) shared with `libSceContentSearch`, which queries an empty host library (`docs/dev/debt/libSceContentSearch.md:9`), and `libSceContentDelete`, which has no content database (`docs/dev/debt/libSceContentDelete.md:5`). The console's output path format and per-call errors would still need measuring.
