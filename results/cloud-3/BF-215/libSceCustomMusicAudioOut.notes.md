# libSceCustomMusicAudioOut (5 todo: D 5)

**What it is:** the audio-output side of the system "custom music" (user BGM) feature: finalize, plus get/set of the music volume and mute state.

**Existing state:**
- 0 done. Five parameterless `NotImplemented_nid_no_patch` declarations: `core/libs/prx/libSceCustomMusicAudioOut/Export.cpp:9`, `:14`, `:19`, `:24`, `:29`.
- No state and no guest test. There is no `CMakeLists.txt`: the library is built from the `prx/*` glob in `core/libs/CMakeLists.txt:146` and `:159`.
- Debt `docs/dev/debt/libSceCustomMusicAudioOut.md:5`: the names and NIDs come from the PS5 3.20 export inventory, no credible prototypes were found, and the declarations are not an ABI claim.
- The sibling `libSceCustomMusicCore` declarations are also parameterless (`docs/dev/debt/libSceCustomMusicCore.md:5`), so the tree has no custom-music model to forward to.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 5.

## Families

- **D (5): unknown signatures over an unmodelled system service.**
  - `Finalize` and `SetAllMusicVolume` change system custom-music state.
  - `GetAllMusicVolume`, `GetMuted` and `GetVolume` return it through outputs whose layout is unknown.
  - None can be C-real: each needs an argument or an output, and the tree holds no console answer.

## Batch proposals

- None: there are no A, B-S, B-M or C-real rows.

## Dependency notes

- Prototypes must be recovered first. Then a custom-music volume/mute model would have to be designed, or the console's "feature unavailable" answer measured.
