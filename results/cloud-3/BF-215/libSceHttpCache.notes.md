# libSceHttpCache (25 todo: C-silent 4, D 21)

**What it is:** a client-side HTTP response cache used with libSceHttp. It covers:
- per-cache init and term;
- requests: create, retrieve, revalidate, set header and data, read data, complete, delete;
- clearing by user or tag, quotas and sharing;
- a system-wide cache at a path (SystemInit, ClearAll, Shutdown, Term, statistics).

**Existing state:**
- 2 done: `sceHttpCacheSystemSendStatistics` and `sceHttpCacheSystemTerm` return 0 without reading arguments (`core/libs/prx/libSceHttpCache/Export.cpp:110`, `:119`; debt `docs/dev/debt/libSceHttpCache.md:5`).
- No cache, request or id state; no error constants; no tests.
- There is no `CMakeLists.txt`: the library is built by the generic loop (`core/libs/CMakeLists.txt:159`). A new test would need a `CMakeLists.txt` in `core/libs/prx/libSceHttpCache/`, modelled on `core/libs/prx/libSceVoiceQoS/CMakeLists.txt:1-16`.
- Declarations:
  - 19 typed declarations with struct layouts (`include/Api.hpp:7-43`, asserted at `Export.cpp:7-16`). No source is cited for them, and the debt only covers the parameterless ones.
  - 5 parameterless declarations with unverified signatures (debt `:6`), plus the parameterless `SystemShutdown`, `SystemTerm` and `SystemSendStatistics`.
- libSceHttp has no network (its `sceHttpCacheRedirectedConnectionEnabled` is an option setter at `core/libs/prx/libSceHttp/src/Options.cpp:133`, unrelated to this module).

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 4, D 21.

## Families

- **C-silent (4), system-wide and global calls with no observable effect:** `SystemInit`, `SystemClearAll`, `SystemShutdown` and `SetCacheSharing`.
  - The tree never stores a cache, so initializing, clearing or shutting down the system cache, or toggling sharing, changes nothing any other call can see.
  - The done `SystemTerm` and `SystemSendStatistics` are the same kind of stub (debt `:5`).
- **D (16), the per-cache and per-request API:**
  - The rows: `Init`, `Term`, `Clear`, `ClearAll` and `SetQuota`; `CreateRequest`, `CreateRequestWithTag`, `DeleteRequest` and `CompleteRequest`; `Retrieve`, `RetrieveWithMemoryPool`, `Revalidate` and `RevalidateWithMemoryPool`; `SetResponseHeader`, `SetData` and `ReadData`.
  - RFC 9111 gives HTTP caching rules (freshness, section 4.2; validation, section 4.3), but not this API's contract, and nothing in the tree does either:
    - how cache and request ids are returned;
    - the error codes;
    - the meaning of `Retrieve`'s `result` and `nextRequest`;
    - the memory-pool `requiredSize` convention;
    - the quota entry names.
  - The typed declarations cite no source.
- **D (5), unknown signatures:** `ClearAllUserTags`, `ClearCacheInfo`, `DebugSetUrl`, `RegisterCacheInfoUrl` and `RemoveTag` (debt `:6`).

## Batch proposals

1. **"httpcache: system-cache silent stubs" (4, C-silent):** `SystemInit`, `SystemClearAll`, `SystemShutdown` and `SetCacheSharing`. Only when a title needs them, as CONTRIBUTING requires.
   - It is small: merge it with other C-silent batches from the census.
   - Files:
     - `core/libs/prx/libSceHttpCache/Export.cpp:80`, `:100`, `:105` and `:114`: return 0 as `:110` and `:119` do;
     - debt `:5`: extend the silent-stub line to name the four, and the title that needs them.
   - Tests: a new `guest_http_cache_tests`:
     - add `core/libs/tests/GuestHttpCache.cpp`;
     - add `core/libs/prx/libSceHttpCache/CMakeLists.txt`, with `add_library` and `add_test_executable` as `core/libs/prx/libSceVoiceQoS/CMakeLists.txt` does;
     - check that each call returns 0, including `SystemInit` and `SystemShutdown` twice and `SystemClearAll` on a path that was never initialized.

## Caveats

- The typed declarations (`include/Api.hpp:47-68`) have no cited source; the debt should say where they came from. If they come from a public SDK binding, the D rows could be re-examined, but the result and error codes would still be needed.
- `Retrieve` and `Revalidate` in an offline model would probably always miss. The console's miss result and the `nextRequest` contents are unknown, so that answer is a guess and the rows stay D.
