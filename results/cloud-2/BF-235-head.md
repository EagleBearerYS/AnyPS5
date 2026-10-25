# BF-235 part 2 head (cloud-2)

**Ready for the profiled Octopath boot:** fork `diag/agc-committer-split` = **bf2cf51b91**, one profile-only commit on upstream `main` **3a2c51afd2** ("Merge pull request #11014", fetched 2026-10-25T14:21Z).

**What the commit adds:** `chore(agc): split the queue-0 committer's time by commit kind in the draw profile`. Under `APS5_PROFILE_DRAW`, every 10 s, next to `[draw] pipelined (10 s, …)`:

```
[draw] committer by kind (10 s): draw N x T us (gpu wait W, staging S, submit B, break proofs P us), dispatch …, dma/copy …, fill …, label/progress …, seal …, other …; in all commits: gpu wait W ms, staging S ms, submit B ms, break proofs P ms of C ms
```

**What each field means:**
- **The kind** is named at each `Enqueue`:
  - draw: `Draw.cpp`, both sites;
  - dispatch: `Dispatch.cpp`;
  - dma/copy: PacketMemory's DMA data store and DMA copy;
  - fill: PacketMemory's DMA fill and BufferFill;
  - label/progress: QueueWorker's `completeSubmission`, PacketExecution's `submitOpenWork` tail and PacketMemory's `commitLabel`;
  - seal: PacketExecution's seal;
  - other: DrawPrecheck's metadata pass, and tests.
  - Kinds with no commits are left out.
- **T** is the mean committer time per commit, the same quantity as `[draw] pipelined`'s "us each on the committer".
- **The four parts** are mean µs per commit of that kind, timed only on the committer thread and only for the outermost scope. A wait inside a staging call counts in both.
  - gpu wait: the Recorder's fence wait in finish, `WaitTimeline` (`WaitSerial`/`WaitRecorded`), `CommandBatch::Wait`, and `CommandBatch`'s `vkQueueWaitIdle` at release;
  - staging: `UploadPrepare`, `UploadFinish`, `RecordStagingCopies`, `RecordCopyBacks` and `WriteBack` (recording the copies, not GPU time);
  - submit: the Recorder's and `CommandBatch`'s `vkQueueSubmit`;
  - break proofs: `GuestMemory::BreakProofs()` after a non-draw commit.
- **Not a field here:** waits for the GPU mutex inside a commit. They are in that thread's `[lock]` line.

**How it was checked:**
- **Build:** CI's configure flags (Ninja, Release, gcc 13.3, ccache, wild linker), with the `libs` target and every executable `^agc` runs: rc 0.
- **`ctest -R '^agc'`** (`--timeout 120 --parallel 4`, as `runner`), run twice:
  - first run: 1009 of 1010; `agc_submit_progress_no_timeline` failed once with "submission lost its real GPU completion store";
  - that test alone, `--repeat until-fail:20`: 20 of 20 passed;
  - second full run: **1010 of 1010** (945 passed, 65 skipped).
  - I read the failure as a load-dependent flake. With the switch off, the change only stores one kind byte per enqueue.
- **Conventions:** `check_conventions.py --base upstream/main` passed, with 0 added comment lines.
- **MinGW `-fsyntax-only`:** all 10 changed `.cpp` files give the same diagnostics as main's copies, ignoring line numbers. The one shared error is this VM's old mingw-w64 headers (`CpuTopology.hpp:93` `EfficiencyClass`). `DrawPipeline.cpp`'s main copy only differs because it was compiled against the new header.

The part 1 and part 3 write-up follows in `results/cloud-2/BF-235.md`.
