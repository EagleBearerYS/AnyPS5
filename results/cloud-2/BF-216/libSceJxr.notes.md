# libSceJxr (3 todo: B-M 1, D 2)

**What it is:** the JPEG XR module. Its PS5 export table has four names, all of which Microsoft's jxrlib also defines: `PKFree`, `PixelFormatLookup`, `readIS` and `writeIS` (`docs/dev/debt/libSceJxr.md:5`, `:9`).

**Existing state** (`core/libs/prx/libSceJxr/Export.cpp`):
- 1 done: `PKFree_nid_postfix` (`:15`), following jxrlib `JXRGlue.c`. It frees `*ppv` with guest `free` and returns `WMP_errSuccess` (`:9`).
- Debt `:9` records that the module being jxrlib is inferred from the export names.
- The three todo rows are `(void)` throwing declarations (`:24`, `:29`, `:34`).
- Test: `guest_jxr_tests` (`core/libs/tests/GuestJxr.cpp`), `core/libs/prx/libSceJxr/CMakeLists.txt:6`, `add_test` `:10`.

**Counts:** A 0, B-S 0, B-M 1, B-L 0, C 0, D 2.

## B family

- **B-M: PixelFormatLookup.** On the jxrlib inference the tree already accepted for PKFree, it is jxrlib's lookup in `JXRGlue.c`:
  - a forward lookup by pixel-format GUID, and a backward lookup by TIFF sample layout, over jxrlib's pixel-format table, filling a `PKPixelInfo`;
  - it needs that table with its GUIDs in guest-readable memory (the result points into it), and `PKPixelInfo` in its LP64 guest layout, pinned with static_asserts;
  - no new dependency; jxrlib is used as the reference, not built.
  - Test in `guest_jxr_tests`: a forward lookup of a known format, a backward TIFF lookup, and an unknown GUID refused.

## D families

- **readIS and writeIS (2):** in jxrlib these are the codec's internal bit-stream input and output, acting on its private encoder/decoder state.
  - This module exports nothing that creates that state, and its layout depends on how the console built jxrlib.
  - Signatures and behaviour are unverified (debt `:5`).

## Batch proposal

None. No family reaches 6.

## Caveats

- **The B-M row rests on the jxrlib inference** of debt `:9`, as PKFree does. The console's PixelFormatLookup signature is still unverified (debt `:5`). Check the jxrlib signature and table against the cited `JXRGlue.c` before implementing.
- **Not re-read here:** the jxrlib details (the function's file, its two lookup kinds, and the codec-internal role of readIS/writeIS) come from the public project the debt cites. There was no network.
