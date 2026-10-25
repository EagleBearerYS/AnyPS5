# libSceVdecsw (1 todo: A 0, B-S 1, B-M 0, B-L 0, C-real 0, C-silent 0, D 0)

**What it is:** the software video decoder (AVC, HEVC, VP9), with input and output queues and per-picture info, on top of libSceVideodec2.

**Existing state** (`core/libs/prx/libSceVdecsw/Export.cpp`, built at `core/libs/prx/libSceVdecsw/CMakeLists.txt:1-3` and linked to libSceVideodec2 at `:8`):
- 17 done.
  - The `decoders` map (`:95-96`) holds `Decoder` objects (`:82-93`).
  - Each keeps the delivered AVC or HEVC infos in `delivered` (`:89`) and the VP9 infos in `deliveredVp9` (`:92`), keyed by frame buffer.
  - `deliveredPictureInfo<TInfo>` (`:168-189`) copies `thisSize - 8` bytes of the matching info. It throws for a null pointer, for a second picture, for another codec's info and for an unknown frame buffer, and returns 0 when there is no picture.
- Errors: OUTPUT_PENDING 0x81510115, INPUT_QUEUE_EMPTY 0x81510116 and DECODE_PENDING 0x81510117 (`:22-24`). Codec ids are at `:25-28`.
- Debt:
  - `:5`: layouts, checked against PPSA05684's decoder;
  - `:7`: the VP9 getter, which follows SharpProspero, and the citation of [shadPS4's `sceVdecswGetPictureInfo`](https://github.com/shadps4-emu/shadPS4/blob/15abf68c9b52c4b04d094f2837719fdb871021f3/src/core/libraries/videodec/vdecsw.cpp#L303-L334).
- Test: `guest_vdecsw` (`core/libs/tests/GuestVdecsw.cpp`, built at `CMakeLists.txt:11` and registered at `:15`). It covers:
  - AVC info (`:93-96`);
  - HEVC info (`:139-145`);
  - the AVC getter throwing on an HEVC picture (`:146-153`);
  - VP9 info (`:286-372`).

## Families

- **B-S (1): `sceVdecswGetPictureInfo(const OutputInfo*, void* first, void* second)` (`:347`).**
  - It is the codec-generic getter. In shadPS4 it copies the delivered picture's info, and the Avc and Hevc getters forward to it (`vdecsw.cpp#L303-L347`).
  - Here, look up `output->frameBuffer` in `delivered` and copy whichever variant is stored, through the same size and pointer checks as `deliveredPictureInfo`.
  - It is not A: forwarding to `sceVdecswGetAvcPictureInfo` (`:355`) would throw for HEVC pictures, as the test asserts (`:146-153`).

## Batch proposal

**"feat(libSceVdecsw): generic picture info" (1 row).**
- Implementation: as above, reusing `deliveredPictureInfo`'s checks.
  - No picture: return 0 and write nothing.
  - Throw for:
    - a null pointer or `thisSize` below 8;
    - a second picture;
    - a frame buffer that no decoder returned;
    - a VP9 picture, since no source shows the generic getter serves VP9.
- Tests: extend `guest_vdecsw_tests`:
  - in `testDecode` and `testHevc`, call `sceVdecswGetPictureInfo(&output, &info, nullptr)` with an AVC or HEVC info and compare the bytes with the codec-specific getter;
  - an output with `pictureCount` 0 returns 0 and leaves a canary untouched;
  - a VP9 picture throws.
- Claims: PR 9441 (closed) implemented this row and PR 10381 (open) changes it. Run `tools/pr_overlap.py` before opening.

## Caveats

- Whether the console's generic getter also serves VP9 is unknown. shadPS4 has no VP9 decoder, and the tree's VP9 getter comes from SharpProspero (debt `:7`).
- shadPS4 returns ARGUMENT_POINTER and STRUCT_SIZE errors where the tree throws. The tree's convention (debt `:7`: "console error codes are unknown") is kept.
