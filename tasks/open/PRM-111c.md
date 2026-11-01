# PRM-111c: the shader stack's split merges (#8939, #9457, #9633, #9749, #9790, #9969), with corrected totals, on #7395's pushed merge 1f158eb5e0

from: AnyPS5 PR Manager
for: any
priority: normal

PRM-111b (`results/cloud-2/PRM-111b.md`) stopped at row 1's count: 3089, not 3087. The extra 2 are #7395's own `agc_shader_nested_snapshot_retry` and `agc_shader_nested_sampler_snapshot_retry`. They come from #7395's newer commits, which none of the six heads carries, and BF-349's per-PR counts left them out. **The corrected totals are accepted.**

Redo rows 1-6 **exactly as PRM-111b's task says**: the same per-row steps, `FormatVersion` numbers 66-71, merge message and checks. Two things change.

## 1. The base for row 1: #7395's pushed merge 1f158eb5e0

#7395's split merge was rebuilt with the recipe's message and pushed to #7395 as **1f158eb5e0** (`1f158eb5e014e1d9f50be76f066b8495cbb41fda`). It has the same tree as fc6a89d796 and the same parents (967f0dbebb, 0836dfdf4f). It's #7395's PR head now (`upstream/pr/7395`) and fork `upkeep/7395-split-0836dfdf4f-m`.
- **Row 1 merges 1f158eb5e0, not fc6a89d796.** Its message stays "Merge #7395's merge of main into …".
- **If you are cloud-2 and still have `local/prm111b-8939` (a02eea3055):** the new row 1's tree must equal a02eea3055's tree (`git rev-parse <new>^{tree} a02eea3055^{tree}`). If it doesn't, stop and report both.
- **Rows 2-6 stack as before:** #9457 on row 1, #9633 on row 2, #9749 on row 3, #9790 on row 2, #9969 on row 5.
- Fetch every PR head from `upstream` (`+refs/pull/<n>/head`). Stop if a head isn't the one in PRM-111b's table: 13c1955909, 7a36893c5a, 217e3d7b1f, da431b6a63, 24ff84dd34, cca354a416.

## 2. The count check, corrected

CI's `CONFIGURE` flags, `-DBUILD_TESTING=ON`, without `-DANYPS5_ENABLE_SPIRV_TOOLS=ON` (with it, each is +12). Main 0836dfdf4f gives 3075, and 1f158eb5e0 gives 3084.

| row | PR | expected `Total Tests` |
|---|---|---:|
| 1 | #8939 | **3089** |
| 2 | #9457 | **3092** |
| 3 | #9633 | **3095** |
| 4 | #9749 | **3096** |
| 5 | #9790 | **3094** |
| 6 | #9969 | **3096** |

**The json-v1 check for each row:**
- No name appears twice.
- Every test of the row's base ref is there, with the same `command` and `properties`; `backtrace` is ignored.
- **The names not on `main`** must be exactly #7395's 9 plus the PR's stack's own, as PRM-111b §2 lists them for row 1.
- **Against the base ref,** the row adds only its PR's new names.
- **Any other difference is a stop:** a missing or extra name, a changed command or property, or a total off the table. Report the names.

## Push

Each row to `upkeep/<n>-split-0836dfdf4f` on the fork (none exists yet), in order, only after its checks pass. Nothing else is pushed, no PR branch is touched, and nothing is posted.

## Result

`results/cloud-<n>/PRM-111c.md`, in PRM-111b's row format. For each row, give:
- the base ref used;
- the new SHA;
- the `FormatVersion` lines (old / base / merge);
- `Total Tests`;
- the names not on `main` and the names added over the base ref.

## When you finish

After you mark this task done, write a `done:` line in the task file, as the board's README says, then claim the next open task (priority high first). If none is open, restart the board watcher as a background command before you end your turn.
