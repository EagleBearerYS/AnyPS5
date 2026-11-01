# PRM-111d: the shader stack's rows 2-6 (#9457, #9633, #9749, #9790, #9969) over oneandonlydean's merge of main into #9457, 4eaf19b304

from: AnyPS5 PR Manager
for: any
priority: normal

## Start only after PRM-111c

- **Don't claim this** until PRM-111c is in `tasks/done/` and its result pushed row 1 (#8939) to fork `upkeep/8939-split-0836dfdf4f`.
- **PRM-111c's row 2 stops by design.** #9457's head moved: oneandonlydean merged main into it. This task replaces PRM-111c's rows 2-6.
- **If you did PRM-111c:** go straight on to this. Keep your build trees.
- **If PRM-111c's row 1 failed or stopped:** leave this task open.

## What changed

oneandonlydean (a maintainer) pushed **4eaf19b304** (`4eaf19b3049a0cbb27150a015f39842a9d741098`) to #9457's branch at 23:50Z on 10-31: "Merge main into fix/shader-table-family-6191", authored and committed by him.
- **Parents:** 7a36893c5a (#9457's old head) and main **b08c539c50** (`b08c539c505c48341146c705bc72d9b603cdb3a7`), which is newer than the wave's 0836dfdf4f.
- **His tree:**
  - `FormatVersion` stays #9457's 55;
  - `CMakeLists.txt` is main b08c539c50's;
  - #9457's registrations are in `tests/cmake/agc_driver_image_table_tests.cmake`, +19 lines over main.
- **The decision (the user's OK, 11-01): build over his merge.**
  - His commit stays his: it's the first parent of row 2. Never rewrite, rebase or re-commit it.
  - Nothing is asked of him.

## Rows

Fetch every head from `upstream` (`+refs/pull/<n>/head`). If one isn't the head below, stop that row and the rows above it, and report the head you found.

| # | PR (head ref) | head | merges (base ref) | its new `FormatVersion` | push to |
|---|---|---|---|---|---|
| 2 | #9457 (`fix/shader-table-family-6191`) | **4eaf19b304** | row 1's merge (fork `upkeep/8939-split-0836dfdf4f`) | **67** (55 on both 7a36893c5a and 4eaf19b304) | `upkeep/9457-split-b08c539c50` |
| 3 | #9633 (`fix/shader-pointer-chase-closure`) | 217e3d7b1f | row 2's merge | **68** (was 56) | `upkeep/9633-split-b08c539c50` |
| 4 | #9749 (`fix/shader-pointer-chase-split-words-on-c2`) | da431b6a63 | row 3's merge | **69** (was 57) | `upkeep/9749-split-b08c539c50` |
| 5 | #9790 (`fix/shader-nested-table-shared-vsharp-reuse`) | 24ff84dd34 | row 2's merge | **70** (was 59) | `upkeep/9790-split-b08c539c50` |
| 6 | #9969 (`fix/shader-nested-table-child-key`) | cca354a416 | row 5's merge | **71** (was 60) | `upkeep/9969-split-b08c539c50` |

## Row 2 (#9457)

1. `git checkout --detach 4eaf19b304`, then `git -c rerere.enabled=false merge --no-ff --no-commit <row 1's SHA>`.
2. **Expected conflicts:**
   - `core/shader/recompiler/ShaderDiskCache.hpp`: 55 against row 1's 66;
   - `core/libs/prx/libSceAgcDriver/tests/cmake/agc_driver_image_table_tests.cmake`: his fragment (#9457's lines over main) against row 1's (#7395's and #8939's lines over main);
   - possibly other fragments under `tests/cmake/`, for the same reason;
   - possibly `libSceAgcDriver/CMakeLists.txt`. If so, run `python3 tools/agc_test_split.py resolve` (the merged tree's copy) as PRM-111b's step 5 says.
   **Any other conflicted file is a stop** for rows 2-6. That includes a file where main's changes between 0836dfdf4f and b08c539c50 meet #7395's or #8939's. Report the hunks.
3. **`ShaderDiskCache.hpp`:** as PRM-111b's step 3, with `FormatVersion = 67`. `git diff 4eaf19b304 -- <file>` and `git diff <row 1> -- <file>` must each be exactly the one `FormatVersion` line. Then do PRM-111b's step 4 (`git grep -n 'FormatVersion'`, stop on a literal).
4. **Fragment against fragment: the union, each line once.** This is not the split, so `resolve` doesn't apply.
   - Keep every line of both sides, each exactly once. Keep each side's lines in that side's order, with his side's lines first wherever the two sides add at the same place.
   - **After it:**
     - `git diff 4eaf19b304 -- <fragment>` adds only lines that row 1 adds over main 0836dfdf4f's copy;
     - `git diff <row 1> -- <fragment>` adds only lines that 4eaf19b304 adds over main b08c539c50's copy, or lines main itself changed between the two mains;
     - neither diff removes a line.
   - **Stop if** a line differs between the sides in anything but position, or a line would have to be dropped or edited.
5. No unmerged file and no conflict marker in any changed file.
6. **Commit**, as `Yunus Shaikh <75296019+EagleBearerYS@users.noreply.github.com>` for author and committer, unsigned, with no trailers:
   ```
   Merge #8939's merge of main into fix/shader-table-family-6191

   tests/cmake/agc_driver_image_table_tests.cmake conflicts, since both sides add registrations to it. The merge keeps every line of both sides, once.

   ShaderDiskCache's FormatVersion conflicts too, since #7395's merge brings 63. This PR's own format moves from 55 to 67, the lowest number above #7395's 63 that no open PR uses, in the stack's order, so its recompiler change keeps a disk cache format of its own.
   ```
   - Name every fragment that conflicted in the second paragraph.
   - If `resolve` ran, add PRM-111b's split paragraph before it.

## Rows 3-6

Exactly as PRM-111b's "Per row" steps 1-7. Its merge message and the base PRs are as there: #9457 for rows 3 and 5, #9633 for row 4, #9790 for row 6. Two differences:
- **The base ref now carries main b08c539c50** through row 2. A conflict where main's changes between 0836dfdf4f and b08c539c50 meet the row's PR is a stop, like any conflict outside `CMakeLists.txt`, the fragments and `ShaderDiskCache.hpp`.
- **A fragment-against-fragment conflict** gets row 2's step 4 rule, named in the message the same way.

## Checks per row (stop that row and the rows above it on any miss)

- **Nothing else changed:** `git merge-tree --write-tree <head> <base ref>` gives git's own merge. `git diff --name-only <that tree> <new merge>` may list only:
  - `libSceAgcDriver/CMakeLists.txt`;
  - `libSceAgcDriver/tests/cmake/*.cmake`;
  - `ShaderDiskCache.hpp`.
- **`CMakeLists.txt`** is identical to main b08c539c50's.
- **The count check: a fresh baseline.**
  - **Before row 2:** configure main b08c539c50 with CI's `CONFIGURE` flags and `-DBUILD_TESTING=ON`, without `-DANYPS5_ENABLE_SPIRV_TOOLS=ON`. Report its `ctest -N | tail -1` total, and keep its `ctest --show-only=json-v1` list.
  - **Then each row,** configured the same way. The json-v1 rule:
    - No name appears twice.
    - **The base ref's tests:** every one is in the row's list, with the same `command` and `properties`; `backtrace` is ignored.
    - **Row 2 only, the head's tests:** every test of 4eaf19b304 is in it too, under the same rule.
    - **Changes between the mains:** a test main removed or changed between 0836dfdf4f and b08c539c50 is the only exception to those two rules. Name the main commit.
    - **The names not on main b08c539c50** must be exactly:
      - #7395's 9;
      - #8939's own, as PRM-111c's row 1 lists them;
      - the PR stack's own up to this row. For row 2, those are the names 4eaf19b304 has over b08c539c50.
    - **Against the base ref,** each row adds only its PR's new names.
    - **Any other difference is a stop.** Report the names.
  - **Totals:** report each row's total and its makeup, as b08c539c50's total plus the count of names not on main. There's no fixed table this time, since the main is new.
  - Delete the build directory after each row.
- **Identity:** two parents, the head first and then the base ref; author and committer the noreply above; `%G?` = N; no trailers.

## Push

- Push each row to its `upkeep/<n>-split-b08c539c50` branch on the fork, in order, and only after its checks pass.
- Nothing else is pushed. No PR branch is touched, and nothing is posted.
- The PR Manager runs the GitHub verifies.

## Result

`results/cloud-<n>/PRM-111d.md`, in PRM-111b's row format. Give b08c539c50's total first, then for each row:
- the head and base ref used;
- the new SHA;
- each conflict and its resolution, with the fragment diffs against both parents;
- the `FormatVersion` lines (head / base / merge);
- `Total Tests` and its makeup;
- the names not on main b08c539c50, and the names added over the base ref.

## When you finish

After you mark this task done, claim the next open task on the board right away (priority high first, then the lowest id; skip any whose `for:` line names another worker). If none is open, restart the board watcher as a background command before you end your turn. A worker that ends its turn with no task and no watcher is never woken: on 2026-10-15 one sat READY for 30 minutes beside two open tasks.
