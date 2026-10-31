# PRM-111b: #12862 split wave, batch 3 again (cloud-2): stopped at row 1's count check

**Row 1 (#8939) passed every check except one.** Its count gives **Total Tests: 3089**, not the table's **3087**.
- That is a stop for row 1 and every row above it. Rows 2–6 all stack on row 1's merge, so I didn't attempt them.
- **Nothing was pushed.** No PR branch was touched, nothing was posted, and nothing was built or run beyond the configures.

**Why the count is off.** The extra 2 tests are #7395's own: `agc_shader_nested_snapshot_retry` and `agc_shader_nested_sampler_snapshot_retry`.
- They come from #7395's newer commits.
- #8939 doesn't carry them, so BF-349's "own 12" for #8939 doesn't count them.
- The merge correctly keeps all 9 of #7395's tests plus #8939's 5 new ones: 3075 + 9 + 5 = **3089**.
- The table's "3075 + own" leaves out those 2. The same 2 are missing from every row's figure, because none of the six stack heads registers them (§3).

**What the PR Manager needs to decide:** whether the expected totals become the table's + 2: **3089, 3092, 3095, 3096, 3094, 3096**. If so, row 1's merge (unpushed, kept locally) can be pushed as is, and rows 2–6 run.

## Refs

- **Main:** 0836dfdf4f.
- **#7395's split merge:** fork `upkeep/7395-split-0836dfdf4f` = **fc6a89d796**, parents 967f0dbebb and 0836dfdf4f.
- **The six heads:** each is from `upstream` (`refs/pull/<n>/head`, fetched 21:05Z), and each equals the task's old head:
  - #8939 13c1955909;
  - #9457 7a36893c5a;
  - #9633 217e3d7b1f;
  - #9749 da431b6a63;
  - #9790 24ff84dd34;
  - #9969 cca354a416.
- The fork had no `upkeep/{8939,9457,9633,9749,9790,9969}-split-0836dfdf4f` branch before or after this task.
- **Tree:** worktree `/home/user/prm111b`. For the configures, all 24 submodules were linked to checkouts at each tree's own pins (no pin differs between main, fc6a89d796 and row 1). Vulkan-Headers was checked out at main's pin 2fa203425e for this.

## Baseline counts, before row 1

**Configure:**
- CI's `CONFIGURE` flags: `-G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON`, gcc/g++, ccache.
- Then `ctest -N | tail -1` and `ctest --show-only=json-v1`.
- `build-check` is deleted after every configure.

| tree | `CONFIGURE` (the task's numbers) | `CONFIGURE` + `-DANYPS5_ENABLE_SPIRV_TOOLS=ON` (CI's Linux job) |
|---|---:|---:|
| main 0836dfdf4f | **Total Tests: 3075** | Total Tests: 3087 |
| #7395's merge fc6a89d796 | **Total Tests: 3084** | Total Tests: 3096 |
| row 1, a02eea3055 (unpushed) | **Total Tests: 3089** (task: 3087) | Total Tests: 3101 |

- The task's 3075 and 3084 are the counts without `-DANYPS5_ENABLE_SPIRV_TOOLS=ON`. CI's Linux job adds that flag, which registers 12 more tests in each tree.
- Both columns give the same deltas.
- No tree has a duplicate name. The json-v1 name count equals `Total Tests` in each.

## 1. Row 1, #8939, in PRM-109's row format

| # | PR | old head | base ref | new (merge, **not pushed**) | conflicted | fragment changed (+/−) | `FormatVersion` (old / base / merge) | `Total Tests` | outcome |
|---|---|---|---|---|---|---|---|---|---|
| 1 | **#8939** (`fix/shader-pointer-key-lane-masks`) | 13c1955909 | `upkeep/7395-split-0836dfdf4f` (fc6a89d796) | **a02eea3055** (`a02eea30552442399330ac9fdaf83a25de872fa2`), kept as local branch `local/prm111b-8939` | `CMakeLists.txt`, `ShaderDiskCache.hpp` | `tests/cmake/agc_driver_image_table_tests.cmake` +7 −2 | 53 / 63 / **66** | **3089** (expected 3087) | **stop: count** |
| 2 | #9457 (`fix/shader-table-family-6191`) | 7a36893c5a | row 1's merge | not attempted | | | 55 / – / (67) | | stop (above row 1) |
| 3 | #9633 (`fix/shader-pointer-chase-closure`) | 217e3d7b1f | row 2's merge | not attempted | | | 56 / – / (68) | | stop |
| 4 | #9749 (`fix/shader-pointer-chase-split-words-on-c2`) | da431b6a63 | row 3's merge | not attempted | | | 57 / – / (69) | | stop |
| 5 | #9790 (`fix/shader-nested-table-shared-vsharp-reuse`) | 24ff84dd34 | row 2's merge | not attempted | | | 59 / – / (70) | | stop |
| 6 | #9969 (`fix/shader-nested-table-child-key`) | cca354a416 | row 5's merge | not attempted | | | 60 / – / (71) | | stop |

**Row 1 step by step:**
1. **Merge:** `git checkout --detach upstream/pr/8939`, then `git -c rerere.enabled=false merge --no-ff --no-commit fc6a89d796` (merge-base fa4eec0c32).
   - Conflicts: exactly `core/libs/prx/libSceAgcDriver/CMakeLists.txt` and `core/shader/recompiler/ShaderDiskCache.hpp`, the two expected.
2. **`ShaderDiskCache.hpp`:** I took the PR's side (`git checkout --ours`) and set `inline constexpr std::uint32_t FormatVersion = 66;`.
   - `git diff 13c1955909 -- <file>`: exactly `-… = 53;` / `+… = 66;`.
   - `git diff fc6a89d796 -- <file>`: exactly `-… = 63;` / `+… = 66;`.
   - The file has no other difference against either side.
3. **`git grep -n FormatVersion`:** 8 hits. **None compares it with a literal number.**
   - `ShaderDiskCache.hpp:16`: the definition, = 66.
   - `ShaderDiskCache.cpp:1433`, `:1446`, `:1728`, `:1905`, `:1926`: the header write, the header check, the log line, and the two `writer.Value(FormatVersion)` calls.
   - `libSceAgcDriver/tests/ShaderDiskCache.cpp:492` and `:2245`: `static_cast<std::byte>(ShaderDiskCache::FormatVersion - 1u)`. Both use the constant.
4. **`resolve`:** `python3 tools/agc_test_split.py resolve` (main 0836dfdf4f's copy) printed `CMakeLists.txt` and `tests/cmake/agc_driver_image_table_tests.cmake`, and exited 0.
   - Afterwards, `git ls-files -u` was empty.
   - No staged file has a `<<<<<<< `, `=======` or `>>>>>>> ` line.
5. **Commit:** two parents, 13c1955909 then fc6a89d796.
   - Author and committer `Yunus Shaikh <75296019+EagleBearerYS@users.noreply.github.com>`, unsigned (`%G?` = N), no trailers.
   - The message is the task's, with `<base PR>` = #7395, the head ref above, and 53 → 66.

**Row 1 checks:**

| check | result |
|---|---|
| Nothing else changed | `git merge-tree --write-tree 13c1955909 fc6a89d796` = 9330cdabb7. `git diff --name-only 9330cdabb7 a02eea3055` lists only `libSceAgcDriver/CMakeLists.txt`, `libSceAgcDriver/tests/cmake/agc_driver_image_table_tests.cmake` and `recompiler/ShaderDiskCache.hpp`. **Pass.** |
| `CMakeLists.txt` identical to the base ref's | `git diff fc6a89d796 a02eea3055 -- CMakeLists.txt` is empty. **Pass.** |
| The move | **Pass.** #8939's `CMakeLists.txt` diff against its merge-base (fa4eec0c32) is +16 −0. All 16 `+` lines are in the fragment byte for byte, indentation included: 12 `add_test`, the `set_tests_properties`, the `if(WIN32)`, the Windows `set_tests_properties` and the `endif()`. Against the base ref the fragment is +7 −2: the 5 tests that are new over #7395, and #7395's two `set_tests_properties` lines replaced by #8939's longer ones (12 names instead of 7). The only `+` lines that also occur in `CMakeLists.txt` are the generic `    if(WIN32)` and `    endif()`, which main's own blocks use. |
| Count | **Fail.** **Total Tests: 3089, expected 3087.** No duplicate name (json-v1: 3089 names, 0 repeated). |
| Base tests kept | **Pass.** All 3084 tests of fc6a89d796's json-v1 are in row 1's. Every one has the same `command` and `properties`. The only field that differs is `backtrace`, CMake's index into its backtrace graph, which moves when lines move: 1577 tests, nothing else changed. Row 1 adds exactly 5 names. |
| Identity | **Pass** (above). |

## 2. Where the 2 extra tests come from

**Row 1 against main (3075):** 14 new names:
- **#7395's 9**, all already in fc6a89d796:
  - `agc_driver_nested_image_table`, `_budget`, `_large`;
  - `agc_driver_nested_sampler_table`;
  - `agc_driver_pointer_phi_ballot_table`, `_copies_table`, `_sampler_table`;
  - `agc_shader_nested_snapshot_retry`, `agc_shader_nested_sampler_snapshot_retry`.
- **#8939's 5**, new over #7395:
  - `agc_driver_pointer_phi_lane_and_table`, `_lane_not_table`, `_high_table`, `_high_second_lane_table`;
  - `agc_driver_pointer_vsharp_select_table`.

**Why the table is 2 short:**
- BF-349 §2.2's 12 for #8939 is #8939's head against main: #7395's first 7 (the `agc_driver_*` ones it carries) plus its own 5.
- The two `agc_shader_*snapshot_retry` tests are registered in `tests/cmake/agc_shader_memory_tests.cmake` at fc6a89d796 (lines 24–29). They come from #7395's 8 newer commits (7f36e41c45..967f0dbebb), which #8939 doesn't carry (PRM-111 §row 1).
- So the merge has 3075 + 7 + 5 + 2 = 3089. Both tests are kept with fc6a89d796's exact command and properties.

## 3. The other rows: what to expect

None of the six heads registers either `agc_shader_*snapshot_retry` test. `git grep` over each head's CMake files finds no line naming them. All six do register #7395's other 7.

So each row's merge should keep those 2 on top of the table's figure, if each head's BF-349 count already includes the stack below it. I haven't measured this for rows 2–6, because they weren't attempted. The expected totals would be:

| row | PR | table | table + 2 |
|---|---|---:|---:|
| 1 | #8939 | 3087 | **3089** (measured) |
| 2 | #9457 | 3090 | 3092 |
| 3 | #9633 | 3093 | 3095 |
| 4 | #9749 | 3094 | 3096 |
| 5 | #9790 | 3092 | 3094 |
| 6 | #9969 | 3094 | 3096 |

## State

- **Worktree `/home/user/prm111b`:** detached at a02eea3055, clean, with no build directory.
- **Local branch `local/prm111b-8939`** = a02eea3055, unpushed. It can be pushed as is to `upkeep/8939-split-0836dfdf4f` if the corrected total is accepted, or rebuilt in minutes.
- **The `upstream/pr/<n>` refs** for all six stay, for the redo.
