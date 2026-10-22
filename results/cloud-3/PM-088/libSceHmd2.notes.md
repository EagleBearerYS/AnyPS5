# libSceHmd2 (PM-088 classification)

- **What it is:** the PS VR2 API. It covers device open, info and field of view, gaze tracking (results, calibration, object selection), reprojection (layers, mirroring, see-through, events), about 20 `sceHmd2Internal*` calls, and 44 `Hmd2*` handlers. Each handler serves a group of NIDs (some unnamed) through `APS5_EXPORT`, with the measured console answer. 13 done, 113 todo.
- **Model:** `sceHmd2Initialize` always returns `UnsupportedFeature 0x81110016` (`Export.cpp:259`, debt Silent stubs). So `initialized` (`Export.cpp:216`) never becomes true. Device info reports NotDetected, and five size/alignment queries return measured constants.
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 66, D 47.
- **C (66 functions):**
  - Every function routed through `BeforeInitialize` (`Export.cpp:219`): the 44 `Hmd2*` handlers plus 22 named exports. The named ones are gaze calibration and object selection, SetIpdOffset, SetFieldOfView, and the five measured reprojection user-event and current-output calls.
  - Each returns the answer measured before initialization on firmware 13.60 (probes hmd2n/hmd2x, debt). NIDs were checked with the `tools/nid_names.py` hash.
  - That is the only reachable state, because Initialize never succeeds. It is real console behaviour for an uninitialized library; that the library is never initialized follows from the silent-stub Initialize.
  - They are listed as todo only because the post-init branch deliberately calls `NotImplemented_nid_no_patch`. Their answers are already asserted in `guest_hmd2_tests` (`core/libs/tests/GuestHmd2.cpp`).
- **D (47 functions):**
  - 2 gaze result queries and 18 reprojection calls have no measured pre-init answer. Only five reprojection siblings were probed, and their `0x8a72000b` is not assumed for the rest.
  - 8 legacy exports have no symbol or caller in system modules.
  - 19 `sceHmd2Internal*` calls have unknown signatures (debt).
  - Extending the hmd2n probe to the 20 unmeasured gaze and reprojection exports is the cheapest way to move them to C. That needs hardware, so they are D now.
- **Dependencies:** none.
- **The batches below are one decision, not new code.** Maintainers would decide that the never-initialized state is final on this PC, then replace the `NotImplemented_nid_no_patch(name)` in BeforeInitialize's post-init branch with an explicit throw (for example `std::logic_error`). Answers do not change, so `progress.py` would count these as done without any change in behaviour. Keeping the branch as it is is equally valid.
  - **Batch 1 (22 functions, C):** the named `sceHmd2Gaze*`, `sceHmd2Reprojection{Set,Clear}UserEvent*`, `GetCurrentOutput`, `SetFieldOfView` and `SetIpdOffset`. The test adds to guest_hmd2_tests one assertion that `sceHmd2Initialize` still returns 0x81110016, with every pre-init answer unchanged after that failed call.
  - **Batch 2 (44 functions, C):** the `Hmd2BeforeInitialize*`, `Hmd2Write*`, `Hmd2Check*`, `Hmd2Slot*` and `Hmd2Index*` handlers. Same change and same test. It exceeds 40 only because one helper serves all of them. If the cap is strict, split it into BeforeInitialize*+Write* (17) and Check*+Slot*+Index* (27).
