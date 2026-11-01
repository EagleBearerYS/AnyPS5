# PRM-111d: the shader stack's rows 5-6 (#9790, #9969), merging oneandonlydean's #9457 head 4eaf19b304

from: AnyPS5 PR Manager
for: any
priority: normal

**Rewritten at 01:50Z 11-01. No wait on PRM-111c: start any time.**

## Why

oneandonlydean (a maintainer) merged main into #9457, #9633 and #9749 himself. They no longer conflict, and they stay as he left them: rows 2-4 of the wave are dropped. Row 1 (#8939) is PRM-111c.

#9790 and #9969 still conflict. #9790 is stacked on #9457, so row 5 merges **his #9457 head** into #9790, and row 6 merges row 5 into #9969. Neither row depends on row 1.

- **#9457's head, 4eaf19b304** (`4eaf19b3049a0cbb27150a015f39842a9d741098`): "Merge main into fix/shader-table-family-6191", by Dean Galvin, 23:50Z 10-31.
  - Parents: 7a36893c5a (#9457's old head) and main **b08c539c50** (`b08c539c505c48341146c705bc72d9b603cdb3a7`).
  - `CMakeLists.txt` is main's.
  - The stack's registrations are in `tests/cmake/agc_driver_image_table_tests.cmake`.
  - `FormatVersion` is 55.
- His commit is never rewritten, rebased or re-committed. It's an ancestor of both new merges.

## Rows

Fetch every head from `upstream` (`+refs/pull/<n>/head`), never `origin`. If one isn't the head below, stop that row and the row above it, and report the head you found. Do the rows in order, on one machine.

| # | PR (head ref) | head | merges (base ref) | `FormatVersion` | push to |
|---|---|---|---|---|---|
| 5 | #9790 (`fix/shader-nested-table-shared-vsharp-reuse`) | 24ff84dd34 | #9457's head **4eaf19b304** | **59**, its own, unless a conflict forces a change | `upkeep/9790-split-b08c539c50` |
| 6 | #9969 (`fix/shader-nested-table-child-key`) | cca354a416 | row 5's merge | **60**, its own, unless forced | `upkeep/9969-split-b08c539c50` |

## Per row

1. `git checkout --detach upstream/pr/<n>`, then `git -c rerere.enabled=false merge --no-ff --no-commit <base ref>`.
2. **Expected conflicts:**
   - `core/libs/prx/libSceAgcDriver/CMakeLists.txt`, the split;
   - possibly a `tests/cmake/` fragment, where the base already registers the stack's tests.
   **Any other conflicted file is a stop** for that row and the row above it. That includes a file where main's changes up to b08c539c50 meet the row's PR. Report the hunks.
3. `python3 tools/agc_test_split.py resolve` (the merged tree's copy). It must exit 0, and leave no unmerged file and no conflict marker in any changed file.
   - **If a fragment still conflicts** after `resolve`, or `resolve` refuses over one, resolve it to the union: keep every line of both sides, each exactly once, each side's lines in that side's order. Then `git diff <base ref> -- <fragment>` and `git diff <head> -- <fragment>` may only add lines.
   - **Stop if** a line differs between the sides in anything but position, or a line would have to be dropped or edited.
4. **`ShaderDiskCache.hpp`:** the row's own number should merge cleanly: 59 over the base's 55, then 60 over row 5's 59. Then `git diff <head> -- core/shader/recompiler/ShaderDiskCache.hpp` is empty.
   - **If it conflicts instead:** stop that row and report both values. Don't pick a number; the PR Manager picks it from the open-PR scan.
   - Run `git grep -n 'FormatVersion'` as in PRM-111b's step 4, and stop on a literal.
5. **Commit**, as `Yunus Shaikh <75296019+EagleBearerYS@users.noreply.github.com>` for author and committer, unsigned, with no trailers:
   ```
   Merge #<base PR>'s merge of main into <head ref>

   The conflict in core/libs/prx/libSceAgcDriver/CMakeLists.txt from #12862's split is resolved with tools/agc_test_split.py resolve, which moves this PR's test registrations into tests/cmake/.
   ```
   - `<base PR>` is #9457 for row 5 and #9790 for row 6.
   - If step 3's union rule ran, add this paragraph, naming the fragment: "tests/cmake/<fragment> conflicts too, since both sides add registrations to it. The merge keeps every line of both sides, once."
6. **Push** to the row's upkeep branch on the fork, only after its checks pass.

## Checks per row (stop that row and the row above it on any miss)

- **Nothing else changed:** `git merge-tree --write-tree <head> <base ref>` gives git's own merge. `git diff --name-only <that tree> <new merge>` may list only `libSceAgcDriver/CMakeLists.txt` and `libSceAgcDriver/tests/cmake/*.cmake`.
- **`CMakeLists.txt`** is identical to 4eaf19b304's, which is main b08c539c50's.
- **The move:** the PR's own registration lines leave `CMakeLists.txt` and appear in the fragments byte for byte. List the fragments changed, with +/− counts against the base ref.
- **The count check, against a fresh baseline:**
  - **Before row 5:** configure main b08c539c50 and 4eaf19b304 with CI's `CONFIGURE` flags and `-DBUILD_TESTING=ON`, without `-DANYPS5_ENABLE_SPIRV_TOOLS=ON`. Report both `ctest -N | tail -1` totals and keep both `ctest --show-only=json-v1` lists.
  - **Then each row,** configured the same way. The json-v1 rule:
    - No name appears twice.
    - **The base ref's tests:** every one is in the row's list, with the same `command` and `properties`; `backtrace` is ignored.
    - **Against the base ref,** the row adds only its PR's own new names, from the PR's own registration lines.
    - **The names not on main b08c539c50** are the base ref's own plus those.
    - **Any other difference is a stop.** Report the names.
  - **Totals:** report each row's total and its makeup, as b08c539c50's total plus the count of names not on main. The rule decides, not a fixed table.
  - Delete the build directory after each row.
- **Identity:** two parents, the head first and then the base ref; author and committer the noreply above; `%G?` = N; no trailers.

Nothing is built or run beyond the configure. The PR Manager runs the GitHub verifies. Nothing else is pushed, no PR branch is touched, and nothing is posted.

## Result

`results/cloud-<n>/PRM-111d.md`, in PRM-111b's row format. Give b08c539c50's and 4eaf19b304's totals first, then for each row:
- the head and base ref used;
- the new SHA;
- each conflict and its resolution, with any fragment union's diffs against both parents;
- the `FormatVersion` lines (head / base / merge);
- `Total Tests` and its makeup;
- the names not on main b08c539c50, and the names added over the base ref.

## When you finish

After you mark this task done, claim the next open task on the board right away (priority high first, then the lowest id; skip any whose `for:` line names another worker). If none is open, restart the board watcher as a background command before you end your turn. A worker that ends its turn with no task and no watcher is never woken: on 2026-10-15 one sat READY for 30 minutes beside two open tasks.
