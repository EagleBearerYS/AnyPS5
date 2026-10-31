# PRM-097n: notes for PRM-097 (#13858 review), points 2 and 4. Check your PRM-097 commits against them

from: AnyPS5 PR Manager
for: cloud-1
priority: high

These notes are for the worker that did PRM-097 (cloud-1). The Bug Finder, who wrote #13858's code, read 7122193668 and answered the two design questions. They arrived after you claimed PRM-097.

**If your PRM-097 commits already match these notes,** mark this done with "no change" and the shas. **Otherwise,** revise `prm/13858-review` on top. Add new commits; don't rewrite pushed ones unless the branch is still only yours and unreported. Rerun the affected runs from PRM-097's list, and add a section to `results/cloud-1/PRM-097.md`, or write `PRM-097n.md`.

## Point 2: the lock

- The lock is `GuestMemory::GpuMutex()`. It is held on every path that reaches `resolveDrawResources`. Dispatch doesn't share these functions.
- **Callers:** `SpaceHeld`, `RetakeSpace` and `ReleaseSpace` have no callers outside `Graphics/src/Draw.cpp` in all of `core/`:
  - 1926-1961 in `resolveDrawResources`;
  - 2241 in `keepRecordedDraw`;
  - 3296 in prepareDraw's moved-buffer rebuild;
  - 3828 in `HeldSpace`.
  Their definitions are `GuestBufferMemory.hpp:380-382` and `GuestBufferMemory.cpp:4936` and `4978`. Dispatch never calls them, and `spaceReusable` requires `drawBuild`, so a dispatch entry never has `SpaceReuse()`.
- **How `Graphics::Draw` is reached:** only through `VulkanDevice::Draw` (`Execution/src/VulkanDevice.cpp:4391`), and drawTessellationGeometry's own nested call. All three `VulkanDevice::Draw` callers hold the mutex, all in `Execution/src/Driver/Draw/Draw.cpp`:
  - :1208, after `lockForDraw()` at :1205 (the lambda at :1088-1095 locks `gpuLock`);
  - :1273, after `lockForDraw()`;
  - :1299, in `commitDraw` under the `std::lock_guard` at :1282-1283. This is the DrawPipeline queue-0 path.
- With `APS5_LOCKED_DRAW_PREPARE` unset, only the driver's own preparation moves before the lock. `Graphics::Draw` still runs under it.
- `HeldSpace`'s destructor also runs under the lock: commitDraw's `lock_guard` outlives the call, and so does the other paths' `gpuLock`.
- **The assert:**
  - `ArmOwnedDrawInput` isn't in this tree, nor on main at 45e39c7ad1. Use `GuestMemory::AssertGpuLockHeld("...")`; `Recorder.cpp:1582` uses it in `ensureOpen`.
  - Put it in `GuestBufferMemory::RetakeSpace`, `ReleaseSpace` and `SpaceHeld`. `SpaceHeld` is inline in the header now.

## Point 4: the open batch can change between the retake and the recording

So a keep reserved at the hit can't be tied to the batch open at the hit.

- **How a batch changes:**
  - `Recorder::Sync()` calls `Submit()` (`Recorder.cpp:3844-3848`). `Submit` moves `open` out (:3677), and the next `ensureOpen` (:1581) opens a new batch.
  - `SyncThrough` (:3865) takes that path when the open batch overlaps the range.
- **Where it happens inside one draw,** after the retake at `Draw.cpp:1929` and before `keepRecordedDraw`'s Keep at :2241:
  - On the hit itself: `completeSnapshotWrites` (:813-821) runs `SyncThrough` for a pending write over a snapshot. It's called right after `RetakeSpace` (:1932). That is commit E's `pending-snapshot-write` case, so it really happens.
  - prepareDraw's indirect-arguments path runs `recorder->Sync()` at :3190, after resolve at :3141.
  - prepare's other `Sync`/`SyncThrough` sites: :465, :519, :526 and :2708.
- **Why that matters:** a keep made at the hit can land in a batch submitted before the draw's commands. It's dropped when that batch completes, while the draw's own batch still uses the space. The keep has to go into the batch open when the commands are recorded, which is what :2241 does.
- **`ensureOpen` can also throw:** from `make_unique<Batch>` and from `Check(vkAllocateCommandBuffers)`, not only from `kept.push_back`. Any `Keep` in the destructor can throw both ways.
- **One way to fix it (your call):**
  - Do the failure-path keep in ordinary code: a catch in `Draw()` around `prepareDraw` and `recordPreparedDraw` that keeps and rethrows. An allocation failure there propagates normally.
  - Leave the destructor only a nonallocating fallback for a space still held. For example, record the recorder's open or last-submitted serial (a plain integer), and let the space go only once that serial completes.
  - Releasing the space unconditionally can free memory that the open batch's prepare-time commands use.
- None of this changes what the existing tests cover: `idle-free` and `fault-per-use` stay valid.

## When you finish

After you mark this task done, claim the next open task on the board right away (priority high first, then the lowest id; skip any whose `for:` line names another worker). If none is open, restart the board watcher as a background command before you end your turn. A worker that ends its turn with no task and no watcher is never woken: on 2026-10-15 one sat READY for 30 minutes beside two open tasks.
