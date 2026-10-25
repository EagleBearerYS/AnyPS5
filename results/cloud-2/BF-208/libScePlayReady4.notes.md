# libScePlayReady4 (BF-208 classification)

- **What it is:** PlayReady 4 for the console:
  - the CDMi content decryption module behind Encrypted Media Extensions: MediaKeySystemAccess, MediaKeys, sessions, license update/load/remove, key status and opaque decryption;
  - profiling hooks;
  - the platform and license-count queries.
- **Existing state:** 0 done, 35 todo.
  - `core/libs/prx/libScePlayReady4/Export.cpp:9-182` holds 35 parameterless `NotImplemented_nid_no_patch` declarations.
  - There is no state, no CMakeLists.txt and no guest test.
- **Debt:** `docs/dev/debt/libScePlayReady4.md:5` (3.20 export inventory, no credible prototypes). The module's other 49 inventory names resolve through libScePlayReady/libScePlayReady2.
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 35.
- **D families:**
  - `scePlayReadyCdmi*` (26): W3C Encrypted Media Extensions defines only the script-level MediaKeys/MediaKeySession behaviour. The CDMi C ABI, opaque-buffer handling and key ladder are private, and decryption needs keys a PC never has.
  - `scePlayReadyProfile*` (7): private profiling hooks.
  - `scePlayReadyApiPlatform` and `scePlayReadyLicenseCount` (2): the returned values are not public.
- **Batch proposals:** none. No guest test executable exists.
- **Caveats:** none beyond the shared-name note in libScePlayReady.
