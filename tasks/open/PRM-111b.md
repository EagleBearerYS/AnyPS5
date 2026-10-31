# PRM-111b: #12862 split wave, batch 3 again: the shader stack on #7395's merge, with new disk cache format numbers (#8939, #9457, #9633, #9749, #9790, #9969)

from: AnyPS5 PR Manager
for: any
priority: normal

PRM-111 stopped rows 1-6 on a second conflict, `ShaderDiskCache::FormatVersion` in `core/shader/recompiler/ShaderDiskCache.hpp` (`results/cloud-4/PRM-111.md`). The decision is made: each row takes a new number, in stack order. Redo rows 1-6 with **PRM-109's recipe** plus this one named edit. Row 7 (#10846) is done; don't touch it.

**Refs:** main **0836dfdf4f**; #7395's split merge, fork `upkeep/7395-split-0836dfdf4f` = **fc6a89d796**. Fetch each PR's head from `upstream` (`refs/pull/<n>/head`), never `origin`. Do the rows strictly in this order, on one machine.

| # | PR (head ref) | old head | merges (base ref) | its new `FormatVersion` | push to |
|---|---|---|---|---|---|
| 1 | #8939 (`fix/shader-pointer-key-lane-masks`) | 13c1955909 | `upkeep/7395-split-0836dfdf4f` (fc6a89d796) | **66** (was 53) | `upkeep/8939-split-0836dfdf4f` |
| 2 | #9457 (`fix/shader-table-family-6191`) | 7a36893c5a | row 1's merge | **67** (was 55) | `upkeep/9457-split-0836dfdf4f` |
| 3 | #9633 (`fix/shader-pointer-chase-closure`) | 217e3d7b1f | row 2's merge | **68** (was 56) | `upkeep/9633-split-0836dfdf4f` |
| 4 | #9749 (`fix/shader-pointer-chase-split-words-on-c2`) | da431b6a63 | row 3's merge | **69** (was 57) | `upkeep/9749-split-0836dfdf4f` |
| 5 | #9790 (`fix/shader-nested-table-shared-vsharp-reuse`) | 24ff84dd34 | row 2's merge | **70** (was 59) | `upkeep/9790-split-0836dfdf4f` |
| 6 | #9969 (`fix/shader-nested-table-child-key`) | cca354a416 | row 5's merge | **71** (was 60) | `upkeep/9969-split-0836dfdf4f` |

If a PR's upstream head isn't the old head above, stop that row and the rows above it, and report the head you found.

## Per row

1. `git checkout --detach upstream/pr/<n>`, then `git -c rerere.enabled=false merge --no-ff --no-commit <base ref>`.
2. **Expected conflicts:** `core/libs/prx/libSceAgcDriver/CMakeLists.txt` (the split) and `core/shader/recompiler/ShaderDiskCache.hpp` (the number). **Any other conflicted file is a stop** for that row and every row above it.
3. **`ShaderDiskCache.hpp`:** take the PR's side, with `inline constexpr std::uint32_t FormatVersion = <its new number>;`. Nothing else in the file changes. If the merge of that file comes out clean instead, still set the row's number before committing. Afterwards, `git diff <old head> -- <file>` and `git diff <base ref> -- <file>` must each be exactly the one `FormatVersion` line.
4. **Other uses of the number:** `git grep -n 'FormatVersion'` over the whole tree; report every hit. If a test, fixture or doc compares it with a literal number instead of the constant, stop and report the line. Don't change it.
5. `python3 tools/agc_test_split.py resolve` (main's copy). It must exit 0. Then no unmerged file, and no conflict marker in any changed file. If `resolve` refuses because the other file is still unmerged, `git add` `ShaderDiskCache.hpp` first and run it again. If it still refuses, stop.
6. **Commit**, as `Yunus Shaikh <75296019+EagleBearerYS@users.noreply.github.com>` for author and committer, unsigned, with no trailers:
   ```
   Merge #<base PR>'s merge of main into <head ref>

   The conflict in core/libs/prx/libSceAgcDriver/CMakeLists.txt from #12862's split is resolved with tools/agc_test_split.py resolve, which moves this PR's test registrations into tests/cmake/.

   ShaderDiskCache's FormatVersion conflicts too, since #7395's merge brings 63. This PR's own format moves from <old> to <new>, the lowest number above #7395's 63 that no open PR uses, in the stack's order, so its recompiler change keeps a disk cache format of its own.
   ```
   `<base PR>` is #7395 for row 1, #8939 for row 2, #9457 for rows 3 and 5, #9633 for row 4, #9790 for row 6.
7. **Push** to the row's upkeep branch on the fork.

## Checks per row (stop that row and the rows above it on any miss)

- **Nothing else changed:** `git merge-tree --write-tree <old head> <base ref>` gives git's own merge. `git diff --name-only <that tree> <new merge>` may list only `libSceAgcDriver/CMakeLists.txt`, `libSceAgcDriver/tests/cmake/*.cmake` and `ShaderDiskCache.hpp`.
- **`CMakeLists.txt`** is identical to the base ref's.
- **The move:** the PR's own registration lines leave `CMakeLists.txt` and appear in the fragments byte for byte, as in PRM-111's row 7. List the fragments changed, with +/− counts against the base ref.
- **The count check.** This is new and is required, because `resolve` can drop tests silently. Configure the CI flags with `-DBUILD_TESTING=ON`, then take `ctest -N | tail -1` and `ctest --show-only=json-v1`. Main 0836dfdf4f gives 3075 tests, and #7395's merge fc6a89d796 gives 3084; report fc6a89d796's own total once, before row 1. Each row must give exactly the number below: main's 3075 plus the PR's own registrations, from BF-349 §2.2. No test name may appear twice.

  | row | PR | own (BF-349) | expected `Total Tests` |
  |---|---|---:|---:|
  | 1 | #8939 | 12 | 3087 |
  | 2 | #9457 | 15 | 3090 |
  | 3 | #9633 | 18 | 3093 |
  | 4 | #9749 | 19 | 3094 |
  | 5 | #9790 | 17 | 3092 |
  | 6 | #9969 | 19 | 3094 |

  Also check that every test in the base ref's json-v1 list is in the row's with the same command and properties, apart from the PR's own changes. Delete the build directory after each row.
- **Identity:** two parents (the old head, then the base ref), author and committer the noreply above, `%G?` = N, no trailers.

Nothing is built or run beyond the configure. The PR Manager runs the GitHub verifies. No PR branch is touched, and nothing is posted.

The result goes in `results/cloud-<n>/PRM-111b.md`, in PRM-109's row format, plus each row's base ref, its new SHA, the `FormatVersion` line from both parents and the merge, and its `Total Tests`.

## When you finish

After you mark this task done, claim the next open task on the board right away (priority high first, then the lowest id; skip any whose `for:` line names another worker). If none is open, restart the board watcher as a background command before you end your turn. A worker that ends its turn with no task and no watcher is never woken: on 2026-10-15 one sat READY for 30 minutes beside two open tasks.
