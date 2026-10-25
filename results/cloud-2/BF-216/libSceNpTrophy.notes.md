# libSceNpTrophy (2 todo: D 2)

- **What it is:** the PS4 trophy API: contexts and handles, registration, unlocks and unlock state, game, group and trophy info, and icons.
- **Existing state** (`core/libs/prx/libSceNpTrophy/Export.cpp`):
  - 11 done.
    - `Context` (`:58`) is kept per user and service label in `g_contexts` (`:70`); handles in `g_handles` (`:71`).
    - RegisterContext (`:193`) reads the title's `trophyNN.trp` package (`ReadPackage` `:83`). It counts trophies from the `TROPnnn.PNG` icons and groups from the `GRnnn.PNG` icons (`Groups` `:128`).
    - Unlocks persist under `_trophy/` (`LoadState`/`SaveState` `:100`, `:108`).
  - Errors are at `:19-32`. The debt (`docs/dev/debt/libSceNpTrophy.md:5`, `:9-11`) records that the trophy configuration `TROPCONF.ESFM` is encrypted and not read.
  - Test: `guest_np_trophy_tests` (`core/libs/tests/GuestNpTrophy.cpp`), built at `libSceNpTrophy/CMakeLists.txt:6` and registered at `:10`.
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 2.
- **Functions that already behave but count as todo:** both rows are partial.
  - `GetGroupInfo` answers the base group -1 and refuses out-of-range ids. It throws only for an existing group (`:308`).
  - `GetTrophyInfo` answers every trophy of a package without groups. It throws only when the package has groups (`:332`).
- **D:** both gaps need group membership, which only the encrypted `TROPCONF.ESFM` holds. Decrypting it needs a key that neither the tree nor a cited public source provides.
- **Batch proposal:** none.
- **Caveats:** none beyond the debt's. Names, descriptions, grades and hidden flags are already reported empty or unknown for the same reason (debt `:9-11`).
