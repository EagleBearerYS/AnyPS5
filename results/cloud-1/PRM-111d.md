# PRM-111d: the shader stack's rows 5–6 (#9790, #9969), merging oneandonlydean's #9457 head 4eaf19b304 (cloud-1)

**Both rows merged, passed every check, and are pushed.**
- Row 5: #9790 on #9457's head 4eaf19b304 → **a5efbc3ec5**, pushed to `upkeep/9790-split-b08c539c50`.
- Row 6: #9969 on row 5 → **c7b39f3d16**, pushed to `upkeep/9969-split-b08c539c50`.

**Not touched:**
- 4eaf19b304 (Dean Galvin's merge) wasn't rewritten. It's an ancestor of both new merges.
- No PR branch: the `upstream` heads are unchanged after the pushes.
- Nothing was posted, and nothing was built or run beyond the configures.

## Refs

**Heads,** each from `upstream` (`+refs/pull/<n>/head`, fetched 02:01Z and again after the pushes), and each equal to the task's:
- #9457: **4eaf19b304** (`4eaf19b3049a0cbb27150a015f39842a9d741098`), "Merge main into fix/shader-table-family-6191", Dean Galvin, 2026-11-01 12:50:11 +1300. Parents 7a36893c5a and main **b08c539c50**.
- #9790: 24ff84dd34.
- #9969: cca354a416.

**Facts at #9457's head that the task relies on:**
- `CMakeLists.txt` is main b08c539c50's (`git diff b08c539c50 4eaf19b304 -- CMakeLists.txt` is empty).
- The stack's registrations are in `tests/cmake/agc_driver_image_table_tests.cmake`, +19 over main.
- `FormatVersion = 55`.

**Before:** the fork had no `upkeep/9790-split-b08c539c50` and no `upkeep/9969-split-b08c539c50`.

## Baselines, before row 5

**Configure:** CI's `CONFIGURE` flags, without `-DANYPS5_ENABLE_SPIRV_TOOLS=ON`:
- `-G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache`;
- a fresh `build-check` each time, at the tree's own submodule pins, deleted afterwards.

Every configure exited 0, with no `CMake Error`. Each gave the same one CMake dev warning, from `3rdparty/quickjs/CMakeLists.txt:14` (policy CMP0077). Main gives it too.

| tree | `ctest -N \| tail -1` | names not on main |
|---|---|---|
| main **b08c539c50** | **Total Tests: 3081** | — |
| #9457's head **4eaf19b304** | **Total Tests: 3096** | 15 (below) |

**Against main, 4eaf19b304:**
- adds 15 names and removes none;
- changes no test's command or properties;
- has no duplicate name.

The 15:
- `agc_driver_nested_image_table`, `_budget`, `_large`;
- `agc_driver_nested_sampler_table`;
- `agc_driver_pointer_hop_table`;
- `agc_driver_pointer_phi_ballot_table`, `_copies_table`, `_high_second_lane_table`, `_high_table`, `_lane_and_table`, `_lane_not_table`, `_sampler_table`;
- `agc_driver_pointer_vsharp_fixed_offset_table`, `_fixed_table`, `_select_table`.

## The rows, in PRM-111b's row format

| # | PR (head ref) | head | base ref | new (merge) | conflicted | fragment changed (+/−, against the base ref) | `FormatVersion` (head / base / merge) | `Total Tests` | pushed to |
|---|---|---|---|---|---|---|---|---|---|
| 5 | **#9790** (`fix/shader-nested-table-shared-vsharp-reuse`) | 24ff84dd34 | #9457's head **4eaf19b304** | **a5efbc3ec5** (`a5efbc3ec560aa54d859ec8becd304ad33633fff`) | `libSceAgcDriver/CMakeLists.txt` only (1 hunk) | `agc_driver_image_table_tests.cmake` +4 −2 | 59 / 55 / **59** | **3098** = 3081 + 17 | `upkeep/9790-split-b08c539c50` |
| 6 | **#9969** (`fix/shader-nested-table-child-key`) | cca354a416 | row 5's merge **a5efbc3ec5** | **c7b39f3d16** (`c7b39f3d16c4987ff6a385fdbb53bfe2240000d8`) | `libSceAgcDriver/CMakeLists.txt` only (1 hunk) | `agc_driver_image_table_tests.cmake` +4 −2 | 60 / 59 / **60** | **3100** = 3081 + 19 | `upkeep/9969-split-b08c539c50` |

### Row 5, #9790

**Steps:**
1. **Merge:** `git checkout --detach upstream/pr/9790`, then `git -c rerere.enabled=false merge --no-ff --no-commit 4eaf19b304`. The merge base is 7a36893c5a.
   - The only conflict was `core/libs/prx/libSceAgcDriver/CMakeLists.txt`, 1 hunk.
   - No fragment conflicted, and nothing else did.
2. **`resolve`:** `python3 tools/agc_test_split.py resolve` (the merged tree's copy) printed `CMakeLists.txt` and `tests/cmake/agc_driver_image_table_tests.cmake`, and exited 0.
   - Afterwards, `git ls-files -u` was empty, and no changed file has a conflict marker.
   - It applied #9790's change to the fragment the base already had, so the union rule wasn't needed.
3. **`ShaderDiskCache.hpp`:** merged cleanly to #9790's own `FormatVersion = 59`, over the base's 55. `git diff 24ff84dd34 -- core/shader/recompiler/ShaderDiskCache.hpp` is empty.
   - `git grep -n FormatVersion` finds 8 hits: the definition (`ShaderDiskCache.hpp:16`); `ShaderDiskCache.cpp:1433`, `:1446`, `:1728`, `:1905`, `:1926`; and `tests/ShaderDiskCache.cpp:492`, `:2245` (`FormatVersion - 1u`).
   - **None compares it with a literal.**
4. **Commit:** two parents, 24ff84dd34 then 4eaf19b304.
   - Author and committer `Yunus Shaikh <75296019+EagleBearerYS@users.noreply.github.com>`, `%G?` = N, no trailers.
   - The message is "Merge #9457's merge of main into fix/shader-nested-table-shared-vsharp-reuse" plus the task's paragraph. There was no union paragraph, since the union rule didn't run.

**Checks:**

| check | result |
|---|---|
| Nothing else changed | `git merge-tree --write-tree 24ff84dd34 4eaf19b304` = 57c4fcc37b. `git diff --name-only 57c4fcc37b a5efbc3ec5` lists only `libSceAgcDriver/CMakeLists.txt` and `libSceAgcDriver/tests/cmake/agc_driver_image_table_tests.cmake`. **Pass.** |
| `CMakeLists.txt` | Identical to 4eaf19b304's, and to main b08c539c50's. **Pass.** |
| The move | **Pass.** #9790's own `CMakeLists.txt` change against its merge base 7a36893c5a is +4 −2: two `add_test` lines (`agc_driver_pointer_vsharp_shared_table`, `agc_driver_pointer_vsharp_shared_reuse_table`), and the two `set_tests_properties` lines (main and `WIN32`) with those 2 names added. All 6 lines are the fragment's diff against 4eaf19b304, byte for byte, indentation included. The fragment has no other changed line. |
| Count, json-v1 | **Pass.** **Total Tests: 3098**, with no duplicate name. All 3096 of the base's tests are there with the same `command` and `properties` (`backtrace` ignored). It adds exactly #9790's 2: `agc_driver_pointer_vsharp_shared_table`, `agc_driver_pointer_vsharp_shared_reuse_table`. Not on main: 17, the base's 15 plus those 2. 3081 + 17 = 3098. |
| Identity | **Pass** (above). |

### Row 6, #9969

**Steps:**
1. **Merge:** `git checkout --detach upstream/pr/9969`, then `git -c rerere.enabled=false merge --no-ff --no-commit a5efbc3ec5`. The merge base is 24ff84dd34.
   - The only conflict was `core/libs/prx/libSceAgcDriver/CMakeLists.txt`, 1 hunk.
2. **`resolve`:** printed `CMakeLists.txt` and `tests/cmake/agc_driver_image_table_tests.cmake`, and exited 0. Afterwards there was no unmerged file and no conflict marker, and the union rule wasn't needed.
3. **`ShaderDiskCache.hpp`:** merged cleanly to #9969's own **60**, over row 5's 59. `git diff cca354a416 -- core/shader/recompiler/ShaderDiskCache.hpp` is empty.
   - `git grep -n FormatVersion` finds the same 8 uses at #9969's line numbers: `ShaderDiskCache.cpp:1439`, `:1452`, `:1734`, `:1911`, `:1932`, and `tests/ShaderDiskCache.cpp:492`, `:2283`.
   - None is a literal.
4. **Commit:** two parents, cca354a416 then a5efbc3ec5. Same identity, `%G?` = N, no trailers. The message is "Merge #9790's merge of main into fix/shader-nested-table-child-key" plus the task's paragraph.

**Checks:**

| check | result |
|---|---|
| Nothing else changed | `git merge-tree --write-tree cca354a416 a5efbc3ec5` = 56642b9d16. `git diff --name-only 56642b9d16 c7b39f3d16` lists only `libSceAgcDriver/CMakeLists.txt` and `tests/cmake/agc_driver_image_table_tests.cmake`. **Pass.** |
| `CMakeLists.txt` | Identical to row 5's, and to main b08c539c50's (and so to 4eaf19b304's). **Pass.** |
| The move | **Pass.** #9969's own change against its merge base 24ff84dd34 is +4 −2: `add_test` for `agc_driver_pointer_vsharp_child_key_table` and `agc_driver_pointer_vsharp_child_key_arm_table`, plus the two `set_tests_properties` lines with those names added. All 6 are the fragment's diff against a5efbc3ec5, byte for byte, with nothing else. |
| Count, json-v1 | **Pass.** **Total Tests: 3100**, with no duplicate name. All 3098 of row 5's tests are there with the same `command` and `properties`. It adds exactly #9969's 2: `agc_driver_pointer_vsharp_child_key_table`, `agc_driver_pointer_vsharp_child_key_arm_table`. Not on main: 19, row 5's 17 plus those 2. 3081 + 19 = 3100. |
| Identity | **Pass.** |

## The names not on main b08c539c50

- **Row 5 (17):** #9457's 15 above, plus #9790's `agc_driver_pointer_vsharp_shared_table` and `agc_driver_pointer_vsharp_shared_reuse_table`.
- **Row 6 (19):** row 5's 17, plus #9969's `agc_driver_pointer_vsharp_child_key_table` and `agc_driver_pointer_vsharp_child_key_arm_table`.

## Pushes

- `upkeep/9790-split-b08c539c50` = a5efbc3ec5.
- `upkeep/9969-split-b08c539c50` = c7b39f3d16.

Both were confirmed with `git ls-remote origin`. Afterwards `upstream` still has #9457 at 4eaf19b304, #9790 at 24ff84dd34 and #9969 at cca354a416. Nothing else was pushed.

## State

- The clone is detached at c7b39f3d16.
- No `build-check` directory remains.
