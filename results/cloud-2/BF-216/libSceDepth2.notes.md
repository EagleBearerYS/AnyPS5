# libSceDepth2 (10 todo: A 0, B-S 0, B-M 0, B-L 0, C 0, D 10)

Upstream main 2742004ed2.

- **What it is (by export names):** the second-generation depth engine for camera images: initialize, query memory, terminate, save internal buffers; set command, set ROI, submit, wait-and-post-process, get image; and factory calibration data. It is a smaller sibling of libSceDepth (38 todo, all D in BF-208's table).
- **Existing state:** 0 done, 10 todo.
  - `core/libs/prx/libSceDepth2/Export.cpp:9-54` declares all 10 as `(void)` and throws through `NotImplemented_nid_no_patch`. No state, constants or error codes.
  - Debt `docs/dev/debt/libSceDepth2.md:5`: the 10 exports are in the PS5 3.20 export table and were cross-checked against firmware 13.60; "Their signatures and behaviour are unverified".
  - No CMakeLists.txt: the generic rule builds it (`core/libs/CMakeLists.txt:158-161`). No guest test.
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 10.
- **B and C families:** none.
  - It is a computation library over frames and calibration the caller supplies, so "no camera attached" fixes no answer by itself, and every declaration is parameterless.
  - libSceCamera, the only source of frames, throws on `sceCameraOpen` (`core/libs/prx/libSceCamera/Export.cpp:16`), so a title on this PC has nothing to feed it.
- **D families (10):**
  - Lifecycle and memory (4): Initialize, QueryMemory, SaveInternalBuffers, Terminate. Sizes, init parameters and the not-initialized error are not in the tree.
  - Frame processing and submission (5): SetCommand, SetRoi, Submit, WaitAndExecutePostProcess, GetImage. The algorithm and its records are private.
  - Calibration (1): SetCameraFactoryCalibrationData. Record layout unknown.
- **Batch proposal:** none, because there is no A, B or C family. A future change would need a `core/libs/prx/libSceDepth2/CMakeLists.txt` with a guest test, as `libSceDiscMap/CMakeLists.txt:6,10` does.
- **Caveats:**
  - The firmware 13.60 cross-check confirms names, not prototypes.
  - Even with prototypes, the depth algorithm has no public specification; a real implementation would be at least B-L once a source exists.
  - Families are grouped by export name only, the same grouping BF-208 used for libSceDepth.
