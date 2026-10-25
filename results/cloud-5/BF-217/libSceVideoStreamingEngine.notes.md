# libSceVideoStreamingEngine (12 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 12)

**What it is:** the DRM key-management part of the video streaming engine. Its names mirror W3C Encrypted Media Extensions objects: MediaKeySystemAccess, MediaKeys and MediaKeySession (generate request, load, expiration, key statuses, session id, event handler), plus a CDM storage configuration.

**Existing state:**
- Nothing is done. `core/libs/prx/libSceVideoStreamingEngine/Export.cpp:9-67` holds twelve parameterless declarations, each of which throws.
- There is no `CMakeLists.txt`, so `Export.cpp` is built by `core/libs/CMakeLists.txt:159`.
- Debt: `docs/dev/debt/libSceVideoStreamingEngine.md:5` says no credible prototypes were found.
- Tests: none registered.
- Neighbours:
  - libSceEsvmEngine exports the same EME family (`sceEsvmEngineMediaKeySession*`, `sceEsvmEngineMediaKeySystemAccess*`, `sceEsvmEngineMediaKeys*`), all still todo;
  - libScePlayReady4's `scePlayReadyCdmi*` calls are also todo.

## Families

- **D (12).**
  - The EME specification is public, but Sony's C ABI for it is not: handles, strings, buffers, the event-handler type and the CDM storage record are all unknown.
  - Behind the calls sits a DRM content-decryption module (PlayReady or another CDM) and a licence server. Neither is public or present.

## Batch proposal

None. No PR claims these rows.

## Caveats

- Even with a prototype, the honest offline answer would be the CDM-unavailable error of the EME flow. Sony's code for it is not in the tree.
