# libSceEsvmEngine (19 todo: D 19)

**What it is:** an Encrypted Media Extensions style key-system engine. It provides media-key-system access, media keys and key sessions: generate a request, update, load, remove and close, plus key statuses, expiration, event handlers and the server certificate. It is the DRM side of the ESVM media stack.

**Existing state:**
- 0 done. Every export is a parameterless declaration that throws (`core/libs/prx/libSceEsvmEngine/Export.cpp:9-102`, one every 5 lines).
- There is no `CMakeLists.txt` in the library: it is built by the generic loop in `core/libs/CMakeLists.txt:159`. There are no tests, state objects or error constants.
- Debt `docs/dev/debt/libSceEsvmEngine.md:5`: the names and NIDs come from the PS5 3.20 export inventory, and no credible prototypes were found.
- Neighbours:
  - `libSceEsvm` has only three parameterless throwing declarations (`docs/dev/debt/libSceEsvm.md`).
  - The PlayReady CDMI calls with similar names, such as `scePlayReadyCdmiCreateMediaKeySession` (`core/libs/prx/libScePlayReady4/Export.cpp:19`), also throw.
  - No implemented library offers a forwarder.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 19.

## Families

- **D (19), unknown signature and an unmodelled service:**
  - All 19 are parameterless declarations with no credible prototype (debt `:5`).
  - Each one creates, uses or destroys a handle, or fills a record: configuration, key statuses, session id, expiration.
  - Behind them sits a content decryption module that the tree does not model.
  - No answer needs zero arguments and zero output, so none can be C-real.

## Batch proposals

None. No row is A, B or C-real.

## Dependency notes

- An implementation needs the prototypes and a decision on how a title without a key system should fail. W3C EME describes the JavaScript API shape, not this native ABI, so it cannot supply either.
- An offline "key system not supported" answer would also need a console-measured error code, which the tree does not have.
