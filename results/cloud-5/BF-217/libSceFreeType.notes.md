# libSceFreeType (1 todo: A 0, B-S 0, B-M 0, B-L 1, C-real 0, C-silent 0, D 0)

**What it is:** the console's FreeType 2 build, exported to guests. AnyPS5 adapts the guests' LP64 records to the bundled FreeType 2.13.3.

**Existing state:**
- 53 done.
- `FT_Request_Size` lives in `core/libs/prx/libSceFreeType/src/SizeRequest.cpp:11-23`. That file is compiled by `core/libs/prx/libSceFreeType/CMakeLists.txt:13`, and `Export.cpp` by `:2`.
- Guest record `GuestSizeRequestRec` (`include/GuestFreeType.hpp:79-85`): 32 bytes with a 64-bit width and height, asserted at `:185-186`.
- Debt (`docs/dev/debt/libSceFreeType.md:17`): the signature, layout and errors are unverified, and the code follows the LP64 public headers. On Windows, a valid request whose width or height exceeds the 32-bit `FT_Long` throws before the size changes.
- Tests: 11 registered. `guest_freetype_request_size` (`core/libs/tests/GuestFreeTypeRequestSize.cpp`) is built at `CMakeLists.txt:16` and registered at `:20`.

## Families

- **B-L (1): FT_Request_Size_nid_postfix.**
  - The function is done:
    - null and invalid checks (`SizeRequest.cpp:13-14`);
    - the host call and size sync (`:18-22`).
  - The only throw (`:15-16`) needs a host `FT_Long` narrower than 64 bits, which means Windows. On Linux and macOS the condition can never be true.
  - The test pins both sides:
    - on an LP64 host, 2^32+1024 answers `FT_Err_Invalid_Pixel_Size` (`GuestFreeTypeRequestSize.cpp:145-146`);
    - otherwise the call must throw and leave the metrics unchanged (`:147-156`).
  - Why B-L and not B-S: FreeType 2.13.3's `FT_Request_Metrics` (`https://github.com/freetype/freetype/blob/42608f77f20749dd6ddc9e0536788eaad70ea4b5/src/base/ftobjs.c#L3247-L3381`) does not reject every large value.
    - A SCALES request stores the 64-bit width directly as `x_scale` (`#L3283`).
    - The ppem limit (`#L3359`) is checked after `FT_MulFix(units_per_EM, x_scale)`, so a face with a small `units_per_EM` succeeds with a scale that a 32-bit `FT_Fixed` cannot hold.
    - REAL_DIM, BBOX and CELL divide by font extents, so they can also land under the limit.
    - Every later glyph load at that size would need the same 64-bit scale.
  - So the general Windows answer needs a FreeType scaler with 64-bit longs on an LLP64 host. That is a missing subsystem.

## Batch proposal

None. No PR claims this row.

## Caveats

- This is not a Linux gap: titles on Linux never reach the throw. The row counts as todo only because its body still calls `NotImplemented_nid_no_patch`.
- A partial Windows answer exists. A NOMINAL request on a scalable face with width or height above 2^31 gives a ppem above 0xFFFF, so `Invalid_Pixel_Size`, unless the resolution product below overflows. That branch alone would still leave the throw for the other types.
- `FT_REQUEST_WIDTH` multiplies `width` by `horiResolution` in `FT_Pos` (`https://github.com/freetype/freetype/blob/42608f77f20749dd6ddc9e0536788eaad70ea4b5/include/freetype/internal/ftobjs.h#L653-L656`). That product can overflow even on LP64. The debt's "host overflow behavior remains that of the bundled build" covers it.
