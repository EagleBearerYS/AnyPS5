# libSceJpegDec (1 todo: D 1)

**What it is:** the JPEG decoder: query memory, create and delete a decoder, parse a header and decode, plus one extra decode export with "input control".

**Existing state:**
- 5 done: `QueryMemorySize`, `Create`, `Delete`, `ParseHeader` and `Decode`.
  - Code: `core/libs/prx/libSceJpegDec/Export.cpp:147-252`.
  - Decoding uses the in-tree `decoder_jpeg` (`core/libs/prx/libSceJpegDec/CMakeLists.txt:1-3`, `:9`).
- Error constants: `Export.cpp:22-28` (0x80650001 to 0x80650012).
- Tests: `guest_jpeg_dec_tests` (`core/libs/tests/GuestJpegDec.cpp`), registered at `core/libs/prx/libSceJpegDec/CMakeLists.txt:12`, with `add_test` at `:16`.
- Debt `docs/dev/debt/libSceJpegDec.md`:
  - `:5`: `sceJpegDecDecodeWithInputControl` has no credible prototype.
  - `:9-10`: functional gaps of the done functions (decode mode 1, pixel formats 9 to 11, unmatched failure diagnostics).

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 1.

## Families

- **D (1):** `sceJpegDecDecodeWithInputControl` (`Export.cpp:254`) is a parameterless declaration with no credible prototype (debt `:5`). The input-control record that sets it apart from `sceJpegDecDecode` (`:164`) is not known, so the existing decoder cannot be reused for it yet.

## Batch proposals

None. No row is A, B or C-real.

## Dependency notes

- None beyond the prototype. Once a title's call shows the extra record, the body is likely `sceJpegDecDecode` with a different input path, using the existing `decoder_jpeg`.
