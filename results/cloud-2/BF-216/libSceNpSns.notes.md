# libSceNpSns (21 todo: D 21)

- **What it is:** PSN social-network links. Titles create, abort and delete Facebook, Twitch and YouTube requests and read the linked account's access token. The `Int*` calls are the system side (Settings): link status, unlink, system and game tokens, title info and master-token refresh.
- **Existing state** (`core/libs/prx/libSceNpSns/Export.cpp`):
  - 0 done. All 21 bodies are `NotImplemented_nid_no_patch` (`:9` to `:109`), declared without parameters in `include/Api.hpp:7-27`.
  - No state, no error constants, no CMakeLists.txt (built by the generic `Export.cpp` rule, `core/libs/CMakeLists.txt:159`).
  - The Facebook Create, Delete and GetAccessToken exports are in `libSceNpManager/Export.cpp:1037`, `:1042`, `:1047`, also todo.
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 21.
- **D families:** every row is a parameterless 3.20 declaration with no credible prototype (`docs/dev/debt/libSceNpSns.md:5`).
  - Title request lifecycle (9): Facebook Abort, and Twitch and YouTube Create, Delete and Abort. The request-id form (returned or written) and the checks are unknown, and no done SNS request call exists in the tree to follow.
  - Title access tokens (2): Twitch and YouTube GetAccessToken. The token layout and the answer with no linked account or signed out are not in the tree.
  - System-side `Int*` (10): account linking for Settings over a private PSN service. Token and title-info layouts and the link-status encoding are unknown.
- **Batch proposal:** none (no A/B/C rows). No guest test executable exists: the library has no CMakeLists.txt and no file under `core/libs/tests/` calls it.
- **Caveats:**
  - Once a prototype is found, the title GetAccessToken calls are the likely C rows: an unlinked or signed-out answer, if one is measured or found in a public source.
  - `docs/dev/debt/libSceNpSns.md:8` says the three Facebook exports are declared in libSceNpSnsFacebookDialog by #6977. On this tree they are in libSceNpManager (`Export.cpp:1037-1050`). The debt note is stale.
