# libScePlayReady2 (16 todo: D 16)

**What it is:** the second Microsoft PlayReady DRM module on the console. It adds a buffer helper, decrypt-context cloning, debug output, license deletion and properties, opaque/multiple reader decryption and agent reinitialization to the libScePlayReady set.

**Existing state:**
- 0 done. Sixteen parameterless `NotImplemented_nid_no_patch` declarations: `core/libs/prx/libScePlayReady2/Export.cpp:9-87`, one every five lines from `:9` to `:84`.
- No state, no `CMakeLists.txt` (built from the glob at `core/libs/CMakeLists.txt:146`, `:159`) and no guest test.
- Debt `docs/dev/debt/libScePlayReady2.md:5`: the names and NIDs come from the PS5 3.20 export inventory, no credible prototypes were found, and the declarations are not an ABI claim.
- Debt `:7-8`: the module's other 39 inventory names resolve through `libScePlayReady`'s declarations. Those are all parameterless todo too, classified D in BF-208.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 16.

## Families

All D, for the same root reason: there is no prototype and PlayReady is a private DRM.
- **Reader and decrypt context (8):**
  - `CloneDecryptContext` and `ReaderClose`;
  - `ReaderDecryptMultiple`, `ReaderDecryptMultipleOpaque`, `ReaderDecryptOpaque` and `ReaderDecrypt_Legacy`;
  - `ReaderFreeOpaqueContent` and `ReaderRetrieveOpaqueContent`.

  They need device keys and licenses.
- **License store (4):** `LicenseDelete`, `LicenseDeleteInMemory`, `LicenseGetExpirationTime` and `LicenseGetProperty`. The property ids and store layout are private, and no license exists on a PC.
- **Buffer helper (1):** `BufferInitWithData`. The buffer object layout is not public.
- **Lifecycle (1):** `Reinitialize`. The init parameters are private.
- **Debug (2):** `DebugPrintf` and `DebugSetLevel`. Their argument lists and return conventions are unknown.
  - With a verified prototype they would be C-silent candidates: output-only, and a level nothing reads.
- **No C-real:** a "no DRM service" answer would need a measured console error code, and the tree has none.

## Batch proposals

- None: there are no A, B-S, B-M or C-real rows.

## Dependency notes

- None actionable. A working DRM path needs device certificates and keys a PC does not have. At most, a future measured "unavailable" answer could make some rows C-real.
