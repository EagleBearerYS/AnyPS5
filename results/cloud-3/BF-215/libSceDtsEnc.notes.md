# libSceDtsEnc (7 todo: D 7)

**What it is:** a DTS audio encoder with synchronous and asynchronous encode, flush and wait.

**Existing state:**
- 0 done. Seven parameterless `NotImplemented_nid_no_patch` declarations: `core/libs/prx/libSceDtsEnc/Export.cpp:9`, `:14`, `:19`, `:24`, `:29`, `:34`, `:39`.
- No state, no `CMakeLists.txt` (built from the glob at `core/libs/CMakeLists.txt:146`, `:159`) and no guest test.
- Debt `docs/dev/debt/libSceDtsEnc.md:5`: the names and NIDs come from the PS5 3.20 export inventory, and no credible prototypes were found.
- No library in the tree encodes DTS. `3rdparty/ffmpeg-core` is added at the root `CMakeLists.txt:326`. It is linked by libSceVideodec2, libSceAudiodec, libSceAjm.native and libSceAvPlayer (for example `core/libs/prx/libSceAudiodec/AudiodecLibrary.cmake:11`), none of which encodes DTS. The submodule is not checked out in this tree, so whether its build includes FFmpeg's DCA encoder was not checked.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 7.

## Families

- **D (7): unknown signatures and no encoder.**
  - Encoder lifecycle: `CreateEncoder` and `DeleteEncoder`.
  - Encoding: `Encode`, `EncodeAsync` and `Flush`.
  - Completion: `IsEncoding` and `WaitEncode`.
  - The encoder parameters, handle, buffer descriptors and asynchronous completion model are all unknown.

## Batch proposals

- None: there are no A, B-S, B-M or C-real rows.

## Dependency notes

- An implementation needs prototypes recovered first. It also needs a DTS encoder: FFmpeg's DCA encoder, if `3rdparty/ffmpeg-core` builds it (unchecked); a new third-party submodule at `3rdparty/<name>`, built from source with CMake; or a new subsystem (B-L).
