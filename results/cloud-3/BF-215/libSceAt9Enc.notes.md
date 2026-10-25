# libSceAt9Enc (5 todo: D 5)

**What it is:** the ATRAC9 audio encoder (sysmodule id 0xd1): query work-area size, create, encode, flush, clear context.

**Existing state:**
- 0 done. All five exports throw (`core/libs/prx/libSceAt9Enc/Export.cpp:9`, `:14`, `:19`, `:24`, `:29`).
- Signatures follow Orbital's IDA type database, but `pAt9EncParam` is only a placeholder struct, so its layout is unknown (`docs/dev/debt/libSceAt9Enc.md:6`).
- PPSA03332 imports the library and fails to load without it (debt `:5`).
- No encoder exists in the tree. `3rdparty/LibAtrac9` is built only as a decoder:
  - the `atrac9` target in `core/libs/prx/libSceAjm.native/CMakeLists.txt:3-7`;
  - `Atrac9Decode` is called in `core/libs/prx/libSceAudiodec/src/Codecs.cpp:252`.
- Tests: `guest_at9_enc_tests` (`core/libs/tests/GuestAt9Enc.cpp`) asserts that each export throws. It is registered at `libSceAt9Enc/CMakeLists.txt:6` and `:10`.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 5.

## Families

- **D (5): no encoder and no parameter layout.**
  - `CreateEncoder`, `Encode` and `Flush` need an ATRAC9 encoder and the parameter layout (channels, sample rate, bitrate, superframe settings).
  - `QueryMemSize`'s answer is a console constant that depends on that layout.
  - `ClearContext` resets a work area whose contents are unknown.
  - None of this can be answered from the tree or from a source it cites.

## Batch proposals

- None: there are no A, B-S, B-M or C-real rows.

## Dependency notes

- An implementation needs a new ATRAC9 encoder: a third-party library added as a submodule at `3rdparty/<name>` and built from source, or a new subsystem. The tree cites none, and it would still need the console's `pAt9EncParam` layout and `QueryMemSize` values measured.
