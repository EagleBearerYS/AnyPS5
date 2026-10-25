# libScePngDec (1 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 1)

**What it is:** the PNG decoder. It creates or deletes a decoder in caller memory, parses a header and decodes to RGBA or BGRA.

**Existing state** (`core/libs/prx/libScePngDec/Export.cpp`; built by `CMakeLists.txt:3`, linked to the `decoder_png` component at `:1` and `:7`):
- 5 done: Create (`:116`), Decode (`:134`), Delete (`:186`), ParseHeader (`:193`) and QueryMemorySize (`:205`).
- State: a 24-byte context in caller memory, validated by a self pointer (`:50-58`, `:72-77`).
- Errors: 0x80690001-0x80690012 (`:21-27`).
- Debt (`docs/dev/debt/libScePngDec.md`):
  - `:5`: the work-memory formula, the handle alignment and the validation order are confirmed;
  - `:6`: 16-bit output and pixel format 132 throw;
  - `:10`: the todo row.
- Test: `guest_png_dec` (`core/libs/tests/GuestPngDec.cpp`): executable at `CMakeLists.txt:10`, registered at `:14`. It does not call the todo row.

## Families

- **D (1): `scePngDecDecodeWithInputControl`.**
  - The no-parameter declaration follows shadPS4's `pngdec.h`, which gives no arguments either, and it is not measured (debt `:10`).
  - The input-control parameters, and any streaming callback contract, are unknown. So the done Decode cannot be reused without guessing.

## Batch proposal

None.
- Claims: PR 6361 and PR 6798 (closed) only declared it.

## Caveats

- A prototype must come from a title's call site or another cited source; the shadPS4 link in the debt does not give one.
