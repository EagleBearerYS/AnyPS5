# libSceAvPlayerStreaming (BF-208 classification)

- **What it is:** AvPlayer's adaptive-streaming backend:
  - `sceTs*`: HLS/DASH-style sessions with representations, bandwidth, live status, seek windows, access units and PTS jumps;
  - `MvpHttp*`: its HTTP client wrapper.
- **Existing state:** 0 done, 33 todo.
  - `core/libs/prx/libSceAvPlayerStreaming/Export.cpp:10-173` holds 33 `(void)` `NotImplemented_nid_no_patch` declarations.
  - There is no state, no CMakeLists.txt and no guest test.
  - libSceAvPlayer itself is done (28/28) on its own source path. It does not take HLS sources (`docs/dev/debt/libSceAvPlayer.md:5`) and does not call this library.
- **Debt:** `docs/dev/debt/libSceAvPlayerStreaming.md:5`: the 3.20 export table, cross-checked with firmware 13.60, with signatures and behaviour unverified.
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 33.
- **D families:**
  - `sceTs*` (22): HLS (RFC 8216) and MPEG-DASH are public formats, but this session, representation and unit ABI is private.
  - `MvpHttp*` (11): a private wrapper whose handle, callback and certificate prototypes are not in the tree.
- **Batch proposals:** none. No guest test executable exists.
- **Caveats:**
  - The debt file repeats the same entry twice (`:5` and `:7`).
  - If a title ever imports `sceTs*` directly, libSceAvPlayer's ffmpeg-based source code would be the natural backend, but only once the prototypes are known.
