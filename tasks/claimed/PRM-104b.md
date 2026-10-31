# PRM-104b: redo PRM-104 (#7642 + #9508's 31f5d0e8dd) with the second named edit, `texture.get()` at DeviceImageTable.cpp:88

from: AnyPS5 PR Manager
for: any
priority: high

## Context

- **PRM-104** (`results/cloud-4/PRM-104.md`) merged #9508's **31f5d0e8dd** onto #7642's head **06eba99c50** and stopped.
  - The merged tree doesn't compile at `Graphics/src/DeviceImageTable.cpp:88`, because dd83293174 changed `BindlessHeap::TextureSlot` to take a `Texture*`.
  - With `heap->TextureSlot(kind, texture.get(), view, layout)` at :88, the full build exited 0, and PRM-104 §3-§4's checks hold.
- **The Project Manager OK'd, in the user's name,** exactly two edits beyond keeping both sides:
  1. **The standing `HeapRetention` edit** in `tests/execution/DeviceImageTable.cpp`: remove `BindlessHeap::NoteSweepDue(); BindlessHeap::SweepIfDue();` and reword that check's message to "a texture the table holds keeps its heap slot". The three remaining asserts stay.
  2. **`DeviceImageTable.cpp:88`:** `heap->TextureSlot(kind, texture.get(), view, layout)`. `Slots::take` keeps its `std::shared_ptr<Texture>` signature.
- **The `docs/dev/ARCHITECTURE.md` conflict** gets the keep-both resolution from PRM-104 §1: 31f5d0e8dd's line with #7642's two edits.
- If cloud-4 still has PRM-104's saved patch, reuse it. Otherwise redo the merge from upstream refs.

## What to do

1. **Merge:** `git -c rerere.enabled=false merge --no-ff 31f5d0e8dd` on 06eba99c50, with the resolution and the two edits.
   - The merge message names both edits: "keeps both sides; two edits: HeapRetention drops the deleted sweep calls; HeapSlots passes texture.get() to TextureSlot(Texture*) from dd83293174".
   - Push it to fork branch `prm/7642-9508-31f5d0e8dd`. Don't touch the PR branch, and post nothing.
2. **Stop and write it up, with no push,** if:
   - the full build shows anything beyond that one line;
   - any other edit is needed;
   - a run below fails.
3. **Runs** (CI's Linux flags, `-DANYPS5_ENABLE_SPIRV_TOOLS=ON`, lavapipe, as `runner`, `ANYPS5_REQUIRE_VULKAN=1 GALLIUM_OVERRIDE_CPU_CAPS=avx`):
   - #7642's set ×10: PRM-095 §6.3's 38 names, plus `^agc_driver_heap_slots|^agc_driver_image_table|^agc_driver_device_image`;
   - every test from the files 653edfefa1..31f5d0e8dd changes ×10, mapped as PRM-102 §4 did;
   - `^(agc|shader)` once. Name the known flakes (`agc_driver_idle_store_yields`, the `agc_driver_pipeline_journal` cold-compile timeout) and any disk-space failures as such;
   - `tools/check_test_registration.py build`;
   - `tools/check_conventions.py --base 31f5d0e8dd`.
4. **Own diff:** `git diff 31f5d0e8dd <merge>` against `git diff 0ab5f157a8 06eba99c50`. Expect exactly four differences: the ARCHITECTURE line's placement, the two removed sweep lines, the reworded message, and the `texture.get()` line. List anything else.

## Rules

- Merge commit author and committer `Yunus Shaikh <75296019+EagleBearerYS@users.noreply.github.com>`, unsigned, no trailers. No comments in code.
- Upstream refs come from the remote named `upstream`. Push only to the fork.

## Result

Write `results/cloud-<n>/PRM-104b.md` with:
- the merge SHA;
- the resolution and both edits;
- the build result;
- each run's ctest summary line, with pass/skip/fail and skip reasons;
- the checks' output lines;
- the own-diff differences;
- the identity line;
- the count of added comment lines.

## When you finish

After you mark this task done, claim the next open task on the board right away (priority high first, then the lowest id; skip any whose `for:` line names another worker). If none is open, restart the board watcher as a background command before you end your turn. A worker that ends its turn with no task and no watcher is never woken: on 2026-10-15 one sat READY for 30 minutes beside two open tasks.
