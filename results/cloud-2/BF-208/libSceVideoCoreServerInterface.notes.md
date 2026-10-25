# libSceVideoCoreServerInterface (BF-208 classification)

- **What it is:** the client side of the system VideoCore media service, which plays video for system and web UI (streams, media-source buffers, thumbnails, decoder arbitration, display and audio settings).
- **Existing state:** 0 done, 47 todo.
  - `core/libs/prx/libSceVideoCoreServerInterface/Export.cpp:9-242` holds 47 parameterless `NotImplemented_nid_no_patch` declarations.
  - There is no state, no CMakeLists.txt and no guest test. The sibling `libSceVideoCoreInterface` (4 todo) is also only declarations.
- **Debt:** `docs/dev/debt/libSceVideoCoreServerInterface.md:5` (3.20 export inventory, no credible prototypes).
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 47.
- **D families (all private service IPC with no prototype):**
  - stream and request control (11);
  - media-source buffers (11): W3C Media Source Extensions fixes the script-level semantics only;
  - display and audio settings (9);
  - server launch, setup and event callbacks (6);
  - thumbnails (6);
  - decoder resource arbitration (4).
- **Batch proposals:** none. No guest test executable exists.
- **Caveats:** a game title is not expected to import this library. The tree records no importing title.
