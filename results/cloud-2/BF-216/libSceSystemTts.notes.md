# libSceSystemTts (5 todo: A 0, B-S 0, B-M 0, B-L 0, C 0, D 5)

- **What it is:** going by the export names, the client of the system's accessibility text-to-speech (the screen reader). It covers a speak request, cancel, callback registration and removal, and an "is accessibility available" predicate.
- **Existing state:** 0 done.
  - All 5 bodies in `core/libs/prx/libSceSystemTts/Export.cpp` (lines 9-32) are parameterless `NotImplemented_nid_no_patch` stubs. There is no state.
  - `docs/dev/debt/libSceSystemTts.md:5` lists all 5 as names from the PS5 3.20 export inventory with "no credible prototypes", and says the declarations are "not an ABI claim".
  - There is no CMakeLists.txt. The default branch of the prx glob builds the library (`core/libs/CMakeLists.txt:159`). No guest test exists.
- **Neighbour:** the game-facing libSceTextToSpeech2 (8 done) has no speech engine and returns the kernel's EOPNOTSUPP (`core/libs/prx/libSceTextToSpeech2/Export.cpp:24`). Its debt says "the console's error for an unavailable speech engine is unknown" (`docs/dev/debt/libSceTextToSpeech2.md:9`).
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 5.
- **D families (all parameterless, no prototype):**
  - Speech (2): SpeakRequest and Cancel.
  - Callbacks (2): RegisterCallback and UnregisterCallback.
  - Predicate (1): IsAccessibilityAvailable. "Not available" would be the natural PC answer, but whether it is a return value or a written output, and its encoding, are unknown.
- **Batch proposal:** none (no A, B or C rows), and no guest test exists.
- **Caveats:**
  - TextToSpeech2's EOPNOTSUPP is a project choice, not a measurement, so it cannot carry over as a C answer.
  - Once a prototype is confirmed, IsAccessibilityAvailable answering "not available" would be the first C candidate.
