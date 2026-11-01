# PRM-111c: the shader stack's row 1 split merge (#8939), on #7395's pushed merge 1f158eb5e0 (cloud-4)

**Row 1 is merged, checked and pushed: `upkeep/8939-split-0836dfdf4f` = 3d14230a7c.** Every check of PRM-111b's per-row list passes with the corrected total, **3089**.

Rows 2-6 were not touched, as the scope change says. No PR branch was touched, nothing was posted, and nothing was built or run beyond the configures.

## Refs

- **Main:** 0836dfdf4f, the wave's main. Today's `upstream/main` is b14af0b8c3; it was not used.
- **#8939's head** from `upstream` (`+refs/pull/8939/head`) = **13c1955909**, as the task requires.
- **The base ref:** **1f158eb5e0** (`1f158eb5e014e1d9f50be76f066b8495cbb41fda`). It is `upstream/pr/7395` and fork `upkeep/7395-split-0836dfdf4f-m`, both read at 01:59Z.
  - Parents: 967f0dbebb, 0836dfdf4f.
  - Tree b42821041d, the same as fc6a89d796's (`upkeep/7395-split-0836dfdf4f`).
  - Message "Merge main into feat/shader-nested-sampler-tables".
- **The cloud-2 tree comparison doesn't apply:** I am cloud-4 and never had `local/prm111b-8939` (a02eea3055).
  - Row 1's tree is **8c1595ab26**. Its fragment change (+7 −2), its `FormatVersion` lines and its counts match what PRM-111b reports for a02eea3055.
- `upkeep/8939-split-0836dfdf4f` did not exist on the fork before the push.

## Row 1, in PRM-111b's row format

| # | PR | old head | base ref | new (merge) | conflicted | fragment changed (+/−) | `FormatVersion` (old / base / merge) | `Total Tests` | outcome |
|---|---|---|---|---|---|---|---|---|---|
| 1 | **#8939** (`fix/shader-pointer-key-lane-masks`) | 13c1955909 | **1f158eb5e0** (`upstream/pr/7395`) | **3d14230a7c** (`3d14230a7c297a078970566b8dba5186f0e5e926`) | `CMakeLists.txt`, `ShaderDiskCache.hpp` | `tests/cmake/agc_driver_image_table_tests.cmake` +7 −2 | 53 / 63 / **66** | **3089** (expected 3089) | **pushed** to `upkeep/8939-split-0836dfdf4f` |

**Step by step:**
1. **The merge:** `git checkout --detach 13c1955909`, then `git -c rerere.enabled=false merge --no-ff --no-commit 1f158eb5e0` (merge-base fa4eec0c32).
   - Conflicts: exactly `core/libs/prx/libSceAgcDriver/CMakeLists.txt` and `core/shader/recompiler/ShaderDiskCache.hpp`.
2. **`ShaderDiskCache.hpp`:** `git checkout --ours`, then `inline constexpr std::uint32_t FormatVersion = 66;`.
   - `git diff 13c1955909 -- <file>`: exactly `-… = 53;` / `+… = 66;`.
   - `git diff 1f158eb5e0 -- <file>`: exactly `-… = 63;` / `+… = 66;`.
   - It was staged before `resolve`.
3. **`git grep -n FormatVersion`:** 8 hits, and **none compares it with a literal number.**
   - `ShaderDiskCache.hpp:16` = 66.
   - `ShaderDiskCache.cpp:1433`, `:1446`, `:1728`, `:1905` and `:1926` (header write, header check, log line, two `writer.Value(FormatVersion)`).
   - `libSceAgcDriver/tests/ShaderDiskCache.cpp:492` and `:2245`, both `ShaderDiskCache::FormatVersion - 1u`.
4. **`resolve`:** `python3 tools/agc_test_split.py resolve` (main's copy) printed `CMakeLists.txt` and `tests/cmake/agc_driver_image_table_tests.cmake`, and exited 0.
   - Afterwards `git ls-files -u` was empty, and no changed file had a conflict-marker line.
5. **The commit:** the task's message, with `<base PR>` = #7395, the head ref above and 53 → 66. The first line is "Merge #7395's merge of main into fix/shader-pointer-key-lane-masks".

## Checks

| check | result |
|---|---|
| Nothing else changed | `git merge-tree --write-tree 13c1955909 1f158eb5e0` = ae90843234. `git diff --name-only ae90843234 3d14230a7c` lists only `libSceAgcDriver/CMakeLists.txt`, `libSceAgcDriver/tests/cmake/agc_driver_image_table_tests.cmake` and `recompiler/ShaderDiskCache.hpp`. **Pass.** |
| `CMakeLists.txt` identical to the base ref's | `git diff 1f158eb5e0 3d14230a7c -- CMakeLists.txt` is empty. **Pass.** |
| The move | **Pass.** #8939's `CMakeLists.txt` diff against fa4eec0c32 is +16 −0, and all 16 `+` lines are in the fragment byte for byte. Against the base ref, the fragment is +7 −2: the 5 new `add_test` lines, and #7395's two `set_tests_properties` lines (all-platform and Windows) replaced by #8939's, which name its 12 tests. |
| Count | **Pass.** Main 0836dfdf4f: **Total Tests: 3075**. Base 1f158eb5e0: **Total Tests: 3084**. Row 1: **Total Tests: 3089**. |
| json-v1 | **Pass.** No name appears twice in any of the three lists (3075, 3084 and 3089 names). All 3084 tests of the base ref are in row 1, with the same `command` and `properties` (`backtrace` ignored). All 3075 of main's are there too, unchanged. |
| Names not on `main` | exactly 14, PRM-111b §2's list: #7395's 9 (`agc_driver_nested_image_table`, `_budget`, `_large`, `agc_driver_nested_sampler_table`, `agc_driver_pointer_phi_ballot_table`, `_copies_table`, `_sampler_table`, `agc_shader_nested_snapshot_retry`, `agc_shader_nested_sampler_snapshot_retry`) and #8939's 5 below. **Pass.** |
| Names added over the base ref | exactly #8939's 5: `agc_driver_pointer_phi_lane_and_table`, `agc_driver_pointer_phi_lane_not_table`, `agc_driver_pointer_phi_high_table`, `agc_driver_pointer_phi_high_second_lane_table`, `agc_driver_pointer_vsharp_select_table`. **Pass.** |
| Identity | two parents (13c1955909, then 1f158eb5e0); author and committer `Yunus Shaikh <75296019+EagleBearerYS@users.noreply.github.com>`; `%G?` = N; no trailer. **Pass.** |

**The configures:**
- CI's `CONFIGURE` flags (`-G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON`, gcc/g++, ccache), without `-DANYPS5_ENABLE_SPIRV_TOOLS=ON`.
- Each in `build-check` at the tree's own submodule pins, exit 0, deleted afterwards.

## State

- **The push:** `git ls-remote origin` shows `refs/heads/upkeep/8939-split-0836dfdf4f` = 3d14230a7c297a078970566b8dba5186f0e5e926.
- **The clone** is back at 0836dfdf4f, clean. The local fetch refs were removed; `upstream/pr/8939` and `upstream/pr/7395` stay.
