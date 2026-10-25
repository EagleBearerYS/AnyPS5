# libSceVideodec2 (14 todo: D 14)

**What it is:** the PS5 video decoder API for H.264, HEVC and VP9 elementary streams: compute queue, memory query, decoder create/delete, decode, flush and reset, and per-picture information.

**Existing state** (`core/libs/prx/libSceVideodec2/Export.cpp` unless noted):
- 12 done, all `_nid_postfix`:
  - compute queue (`:479`, `:486`, `:494`) and `QueryDecoderMemoryInfo` (`:499`);
  - `CreateDecoder` (`:513`, H.264, HEVC 974921, VP9 2382845 through FFmpeg) and `DeleteDecoder` (`:526`);
  - `Decode` (`:533`), `Flush` (`:602`) and `Reset` (`:616`);
  - `GetPictureInfo` (`:636`), `GetAvcPictureInfo` (`:678`, a forward to GetPictureInfo) and `GetVp9PictureInfo` (`:682`).
- Decoder state: `Decoder` (`:209`) holds the FFmpeg context, `pictures` by frame buffer (`:216`) and `inputs` by key (`:221`). The globals `decoders` (`:239`) and `pictureOwners` (`:240`) map a frame buffer back to its decoder.
  - Each `Picture` (`:181`) keeps pts/dts/attached, the key-frame flag, VP9 info, crop, `hevcIdr` and the bit depth.
- Internal readers `Videodec2PictureHeader_nid_no_patch` (`:708`) and `Videodec2PictureCrop_nid_no_patch` (`:720`) give that state to libSceVdecsw (`core/libs/prx/libSceVdecsw/Export.cpp:107`).
- The 14 todo rows are parameterless throwing declarations in `Unimplemented.cpp:7-61`. Only `MapDirectMemory` has parameters, `(void*, const void*)`.
- Tests (`core/libs/prx/libSceVideodec2/CMakeLists.txt`): `guest_videodec2_tests` (`core/libs/tests/GuestVideodec2.cpp`, `:12`, `add_test` `:16`), and `guest_videodec2_unimplemented_tests` (`GuestVideodec2Unimplemented.cpp`, `:22`/`:26`), which asserts that all 14 report "not implemented".

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 14.

## The function from the title report: sceVideodec2GetHevcPictureInfo (PPSA02225)

- **Why D:** the declaration is parameterless because no prototype is known. The debt says that only `MapDirectMemory` has a known parameter list (`docs/dev/debt/libSceVideodec2.md:8`), and that `SceVideodec2HevcPictureInfo` is not filled (`:14`). The tree records no PPSA02225 call site for it. PPSA02225 appears only in the Ajm and NpAuth debt files and in libSceAgcDriver tests.
- **Neighbours that make it small once the prototype is known:**
  - State: every field needed is already stored per picture.
    - `Picture` (`Export.cpp:181-190`) holds pts, dts, attached, the key frame (`idr`, from `AV_FRAME_FLAG_KEY`, `:448`), `hevcIdr` (from `StartsHevcIdr`, `:595`), crop (`:449`) and `bitDepthMinus8` (`:455`).
    - `Decoder` holds `profile` and `level` (`:213-214`, set at `:452-453`).
    - `Videodec2PictureHeader_nid_no_patch` (`:708-718`) already returns all of it.
  - Layout and mapping: libSceVdecsw already fills a 0xb8 `HevcPictureInfo` from exactly that header.
    - The fill is in `core/libs/prx/libSceVdecsw/Export.cpp:110-135`, the struct in `include/Api.hpp:95-137`, and the static_assert in `Export.cpp:34`. The layout is shadPS4's `vdecsw.h` (`docs/dev/debt/libSceVdecsw.md:5`).
    - It maps timestamps, the coded size, bit depth, `hevcIdr` to `idrPictureFlag`, the key frame to `irapPictureFlag`, the profile and level, and the conformance window.
    - Libraries already share layouts: libSceVdecsw reuses Videodec2's input, memory and VP9 structures (debt `libSceVdecsw.md:5`).
  - Entry point: `GetPictureInfo` throws for HEVC at `Export.cpp:659`. `GetAvcPictureInfo` is a one-line forward (`:678`). `GetVp9PictureInfo` (`:682-706`) shows the owner lookup and the `thisSize`-preserving copy.
- **What it would need:**
  1. The argument list, from PPSA02225's call site (registers and the `thisSize` it passes), as debt `libSceAjm.native.md:14` records for that title's Ajm call.
     - Candidates in the tree: `(output, info)`, as `sceVdecswGetHevcPictureInfo` (`core/libs/prx/libSceVdecsw/Export.cpp:360`) and `GetVp9PictureInfo` take; or `(output, first, second)`, as `GetAvcPictureInfo` takes.
     - A two-pointer body that ignores a third argument would serve both, but which one is right is not established.
  2. Confirmation that Videodec2's HEVC info is the 0xb8 Vdecsw layout. The `thisSize` at the call site would show it.
  3. Then B-S: add an HEVC branch beside `Export.cpp:659` that writes the Vdecsw-mapped fields. `GetPictureInfo`'s HEVC throw closes at the same time.
     - Test: the HEVC streams `GuestVideodec2.cpp` already decodes, `HevcMain10Stream` (`:277`) and `HevcStream` (`:541`), both from `core/libs/tests/HevcFixture.hpp`.
- A lead not checked here (no network): SharpProspero's `Videodec2.cs`, which debt `:16` cites for the VP9 getter, may declare the HEVC getter too.

## D families

- **Library lifecycle and info (4):** `InitLibrary` (debt `:5`), `TermLibrary`, `QueryLibraryInfo` and `QueryDecoderInfo`. Their arguments, records and results are unknown; a constant 0 would be a made-up success.
- **Memory mapping (2):** `MapMemory`, and `MapDirectMemory`, whose `(decoder, memory)` list comes from EVO-PLAYER-PS5 with no field or result semantics. Decoders here do not use guest decoder memory (`Export.cpp:514`).
- **Config presets (4):** `PresetDecoderConfigInfo`, plain and Avc/Hevc/Vp9. The values written into `DecoderConfigInfo` (`include/Api.hpp:43`) are not in any source.
- **HEVC-specific create/query and `CreateDecoderBid` (3):** CreateDecoder (`:513`) and QueryDecoderMemoryInfo (`:499`) already handle HEVC.
  - If a recovered prototype turns out to be theirs with the codec fixed, CreateHevcDecoder and QueryHevcDecoderMemoryInfo become A forwards.
  - The BID variant has no recorded meaning.
- **GetHevcPictureInfo:** see above.

## Batch proposal

None. No A/B/C family. The GetHevcPictureInfo work is one function plus GetPictureInfo's HEVC branch, after the call site is read.

## Caveats

- **Disagreement between two done neighbours:**
  - Videodec2's `AvcPictureInfo` declares `picWidthInLumaSamples` at offset 0x2c (`include/Api.hpp:127`), and `GetPictureInfo` writes the frame width there (`Export.cpp:669`).
  - libSceVdecsw's shadPS4 layout has `picWidthInMbsMinus1` at the same offset (`core/libs/prx/libSceVdecsw/include/Api.hpp:80`, asserted at `Export.cpp:33`) and writes macroblocks minus 1 there (`:149`).
  - Adopting the Vdecsw HEVC layout for Videodec2 would carry the same question of whose layout Videodec2 uses.
- **Input line numbers:** the inputs' `Unimplemented.cpp` lines match the file.
