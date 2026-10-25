# libSceDiscMap (1 todo: A 0, B-S 0, B-M 0, B-L 0, C 1, D 0)

Upstream main 2742004ed2.

- **What it is:** the disc map: for a title installed from disc, it tells whether a byte range of a file is already on the HDD, resolves ranges to bitmap blocks, and reports the package size.
- **Existing state:** 2 done.
  - `sceDiscMapIsRequestOnHDD` (`core/libs/prx/libSceDiscMap/Export.cpp:14`) and the unnamed `ioKMruft1ek` export (`:23`) both return `SCE_DISC_MAP_ERROR_NO_BITMAP_INFO` 0x81100004 (`:8`) without reading any argument or writing any output.
  - Debt `docs/dev/debt/libSceDiscMap.md:5`: "Both exports implement the confirmed no-bitmap path: return `SCE_DISC_MAP_ERROR_NO_BITMAP_INFO` before validating any argument and leave every output unchanged." `:9`: GetPackageSize's `(int64_t, int*, int*)` prototype follows shadPS4 and is not measured.
  - Test: `guest_disc_map_tests` (`core/libs/tests/GuestDiscMap.cpp`), built at `core/libs/prx/libSceDiscMap/CMakeLists.txt:6` and registered at `:10` (`add_test(NAME guest_disc_map COMMAND guest_disc_map_tests)`). It checks both done exports and their untouched outputs.
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 1 (real behaviour), D 0.
- **C (1):** `sceDiscMapGetPackageSize` gives the neighbours' no-bitmap answer: 0x81100004 before any check, `ret1` and `ret2` untouched. This PC never has disc bitmap metadata, the same absent thing the two done exports answer for.
- **Batch proposal:** none (one row). As `fix(libSceDiscMap): report no bitmap from sceDiscMapGetPackageSize`, extend `GuestDiscMap.cpp` with a call whose `ret1`/`ret2` are filled with 0x5a5a5a5a and stay so, and the return is 0x81100004; move the debt `:9` entry to Functional beside `:5`.
- **Caveats:**
  - The neighbours' path is confirmed; this export's is not. A package-size query might not consult the bitmap at all, so the row depends on the console answering the same way for it. A console or module check should confirm it before the PR.
