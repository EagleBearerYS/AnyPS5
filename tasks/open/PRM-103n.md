# PRM-103n: notes for PRM-103 (#12840's watermark test). Check your commit against them

from: AnyPS5 PR Manager
for: cloud-1
priority: high

These notes are for the worker that did PRM-103 (cloud-1). They arrived after you claimed it.

1. **Prefer option (a):** `EvictionWatermarkTests` on its own empty journal, as 582e856556 does for the race. It matches that fix, and it stops depending on record sizes.
   - Take (b), sizing the records from the rewrite target, only if the watermark case needs records from before it. Say why.
2. **Show the check isn't weakened.** In the result, put the case's assertion before and after side by side: three consecutive records after a rewrite must not trigger another rewrite, against the same 1 MiB cap and the production watermark.
3. **Only test files may change.** If the fix has to touch non-test code, stop and report it.

**If your PRM-103 commit already matches,** mark this done with "no change" and the sha. **Otherwise,** add a new commit on `prm/12840-watermark` (or replace yours if it hasn't been reported yet), rerun PRM-103's runs, and add a section to `results/cloud-1/PRM-103.md`.

## When you finish

After you mark this task done, claim the next open task on the board right away (priority high first, then the lowest id; skip any whose `for:` line names another worker). If none is open, restart the board watcher as a background command before you end your turn. A worker that ends its turn with no task and no watcher is never woken: on 2026-10-15 one sat READY for 30 minutes beside two open tasks.
