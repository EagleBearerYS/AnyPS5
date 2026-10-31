# PRM-112b: #12862 split wave, #12840 again: main's merge with #13581's GuestBufferMemory.hpp hunk, the eviction check, a build and the tests

from: AnyPS5 PR Manager
for: any
priority: normal

PRM-112 stopped #12840 on a second conflict, in `core/libs/prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp`, from #13581 (ashwin280802's heap-mirror eviction, merged as 59bd3b677d). See `results/cloud-4/PRM-112.md`. The decision is made: keep both sides, and check that the new eviction can't drop a mirror that holds #12840's pending stores.

**Refs:**
- **Main:** **0836dfdf4f**, the wave's main. Not today's main.
- **The head to merge into:** fork `prm/12840-heap-mirror-case` = **d22d9c1a11** (PRM-108's test-only commit on #12840's 6bec085232). Check that `upstream/pr/12840` is still 6bec085232 and that d22d9c1a11's only parent is 6bec085232. If not, stop and report both.

## Steps

1. `git checkout --detach d22d9c1a11`, then `git -c rerere.enabled=false merge --no-ff --no-commit 0836dfdf4f`.
2. **Expected conflicts:** `libSceAgcDriver/CMakeLists.txt` (2 hunks, the split's) and `Graphics/include/GuestBufferMemory.hpp` (1 hunk). **Any other conflicted file is a stop.**
3. **`GuestBufferMemory.hpp`: keep both.** Main's 4 declarations (`HoldsMirror`, `EvictMirror`, `WasEvicted`, `RecordEvicted`) first, then #12840's lines (`AllowAddressStores`, `NoteAddressStores`, `struct StoreSpan` …), each side byte for byte. No other line changes. `git add` it.
4. `python3 tools/agc_test_split.py resolve` (main's copy). It must exit 0. Then no unmerged file, and no conflict marker in any changed file. If it refuses, stop.
5. **The eviction check, before any commit is pushed.** Read #13581's eviction at the merged tree: `EvictMirror`, `HoldsMirror`, `WasEvicted`/`RecordEvicted`, their callers in `Graphics/src/GuestBufferMemory.cpp`, and what ed3b5393f1 keeps (mirrors with queued refresh blocks). Read #12840's pending state: the pages from `NoteAddressStores`/`NoteStorePages`, the `StoreSpan`s, and where `WriteBack` consumes them.
   - **The question:** can an eviction drop or free a mirror, or its spans, between the time #12840 records pending store pages or spans for it and the `WriteBack` that consumes them? Answer with `file:line` for each step of the reasoning, at the merged tree.
   - **If it can, or if you can't tell from the code: stop.** Push nothing, and report the lines.
   - If it can't, report the lines that show why (for example, the guard that skips a mirror with pending stores, or the order of eviction against write-back).
6. **Commit**, as `Yunus Shaikh <75296019+EagleBearerYS@users.noreply.github.com>` for author and committer, unsigned, with no trailers:
   ```
   Merge main into fix/agc-bda-store-writable-mirror

   The conflict in core/libs/prx/libSceAgcDriver/CMakeLists.txt from #12862's split is resolved with tools/agc_test_split.py resolve, which moves this PR's test registrations into tests/cmake/.

   The conflict in Graphics/include/GuestBufferMemory.hpp from #13581 keeps both sides: its HoldsMirror, EvictMirror, WasEvicted and RecordEvicted declarations, and this PR's AllowAddressStores, NoteAddressStores and StoreSpan.
   ```
7. **Build and test** at the merge, with CI's Linux flags (`-DANYPS5_ENABLE_SPIRV_TOOLS=ON`), lavapipe, as an unprivileged user, with `ANYPS5_REQUIRE_VULKAN=1 GALLIUM_OVERRIDE_CPU_CAPS=avx`:
   - the full build: `ninja -C build`, exit 0;
   - `tools/check_test_registration.py build` and `tools/check_conventions.py --base 0836dfdf4f`;
   - **#12840's set ×10:** `ctest -R '^agc_driver_pipeline_journal|^agc_pipeline_journal|^agc_driver_bda|^agc_driver_graphics$' --repeat until-fail:10 --parallel 4`;
   - **#13581's tests ×10:** the ctest names registered for `tests/execution/ResidentHeapMirror.cpp` (find them in the fragments and list them), `--repeat until-fail:10`;
   - **`^agc` once,** `--parallel 4`, with pass/skip/fail counts.
   - Repeat the same build and the #12840 and #13581 runs with `-DANYPS5_ENABLE_SPIRV_TOOLS=OFF` (×3 is enough there).
   - Any failure is a stop: push nothing, and report the test's output.
8. **Push** to `upkeep/12840-split-0836dfdf4f` on the fork, only if steps 5 and 7 are clean.

## Checks

- **Nothing else changed:** `git merge-tree --write-tree d22d9c1a11 0836dfdf4f` gives git's own merge. `git diff --name-only <that tree> <new merge>` may list only `libSceAgcDriver/CMakeLists.txt`, `libSceAgcDriver/tests/cmake/*.cmake` and `GuestBufferMemory.hpp`.
- **The own diff:** compare the sorted `+`/`−` lines of `git diff $(git merge-base d22d9c1a11 0836dfdf4f) d22d9c1a11` with those of `git diff 0836dfdf4f <new merge>`. The only differences may be the registration move. `CMakeLists.txt` keeps the PR's build rules (its `StorePageGather.comp` custom command and the header in the library sources); they stay byte for byte.
- **The count check.** This is required, because `resolve` can drop tests silently. Configure with `-DBUILD_TESTING=ON`; `ctest -N | tail -1` must give **3080** (main's 3075 plus #12840's 5 added; its 1 changed registration keeps its name, BF-349 §2.2). Use `ctest --show-only=json-v1` to check that no name appears twice, and that each of main's tests keeps main's command and properties apart from the one #12840 changes.
- **Identity:** two parents (d22d9c1a11, then 0836dfdf4f), author and committer the noreply above, `%G?` = N, no trailers.

No PR branch is touched, and nothing is posted.

The result goes in `results/cloud-<n>/PRM-112b.md`, in PRM-109's row format, plus the eviction check with its lines, the run table, and `Total Tests`.

## When you finish

After you mark this task done, claim the next open task on the board right away (priority high first, then the lowest id; skip any whose `for:` line names another worker). If none is open, restart the board watcher as a background command before you end your turn. A worker that ends its turn with no task and no watcher is never woken: on 2026-10-15 one sat READY for 30 minutes beside two open tasks.

claimed-by: cloud-1 2026-10-31T21:02:52Z
done: 2026-10-31T21:41:32Z, result: results/cloud-1/PRM-112b.md
