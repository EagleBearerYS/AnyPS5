# libSceVrTracker2 (8 todo: D 8)

**What it is:** the PS VR2 tracker: initialization, pose, result and coordinate-system queries, play-area boundary, camera frames, maps and diagnostics.

**Existing state:**
- 81 done, in `core/libs/prx/libSceVrTracker2/Export.cpp`, nearly all answering as the console's module does before initialization.
- Errors: `NOT_INITIALIZED` 0x8A740001 and `INVALID_ARGUMENT` 0x8A740006 (`:50-51`); the helper `ClearThenReportNotInitialized` is at `:53-57`.
- `sceVrTracker2QueryMemory` (`:127-144`) is implemented from measurements.
- The library never initializes. Uninitialized responses were measured on firmware 13.60 without a headset (`docs/dev/debt/libSceVrTracker2.md:5`).
- `sceVrTracker2Initialize`'s 72-byte parameter is recovered, but its option meanings are not (debt `:9`).
- The seven anonymous throwing exports have argument slots but unresolved layouts (debt `:10`).
- Tests: `guest_vr_tracker2_tests` (`core/libs/tests/GuestVrTracker2.cpp`; asserts Initialize throws at `:132`), registered at `libSceVrTracker2/CMakeLists.txt:7` and `:11`.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 8.

## Families

- **D (1): `sceVrTracker2Initialize`** (`:146`). The console's no-headset result is not in the tree.
  - `libSceVrTracker`'s `sceVrTrackerInit` (`core/libs/prx/libSceVrTracker/Export.cpp:92`) is the PS VR1 call, with a different parameter and error space, so it is not an A forwarder.
  - libSceHmd2's refusal of initialization is a libSceHmd unsupported-feature error (`docs/dev/debt/libSceHmd2.md:5`), not a VrTracker2 answer.
- **D (7): anonymous exports with partial layouts** (`:540`, `:546`, `:552`, `:558`, `:564`, `:570`, `:576`):
  - calibration output (`jRmuXjsknsI`);
  - mesh requirements (`NBhWy+Gl0tY`);
  - mode/buffer/size (`9R6W33Au4B4`);
  - result (`O8ZkTFtvUL4`);
  - pose by coordinate (`940WPg+WwIk`);
  - telemetry input (`oZDm1bynCaA`);
  - pose parameter/result (`U1VYczwqukY`).
  - The tree's uninitialized answers (0x8A740001 or 0x8A740006) are not stated for these seven, which were left throwing; their console results with valid arguments are unknown.

## Batch proposals

- None: there are no A, B-S, B-M or C-real rows.

## Caveats

- Earlier revisions of the debt (git history, not the pinned tree) recorded console probes: Initialize did not return within a minute without a headset; `9R6W33Au4B4` returned 0x8A740006 for every input tried; the others crashed on null or filled buffers. If those measurements are restored to the debt with their inputs, `9R6W33Au4B4`'s invalid-argument branch could become a partial C-real row. The other seven would stay D.
