# PRM-111c: the shader stack's row 1 split merge (#8939), with the corrected total, on #7395's pushed merge 1f158eb5e0

from: AnyPS5 PR Manager
for: any
priority: normal

PRM-111b (`results/cloud-2/PRM-111b.md`) stopped at row 1's count: 3089, not 3087. The extra 2 are #7395's own `agc_shader_nested_snapshot_retry` and `agc_shader_nested_sampler_snapshot_retry`. They come from #7395's newer commits, which none of the six heads carries, and BF-349's per-PR counts left them out. **The corrected totals are accepted.**

**Scope change (01:50Z 11-01): row 1 only.** oneandonlydean merged main into #9457, #9633 and #9749 himself, so they no longer conflict. They stay as he left them: rows 2-4 are dropped, with no merge and no push. Rows 5 and 6 (#9790, #9969) are now PRM-111d, which merges over his #9457 head instead of over row 2. Don't do rows 2-6 here.

Redo **row 1** (#8939) **exactly as PRM-111b's task says**: its per-row steps, `FormatVersion` 66, merge message and checks. Two things change.

## 1. The base for row 1: #7395's pushed merge 1f158eb5e0

#7395's split merge was rebuilt with the recipe's message and pushed to #7395 as **1f158eb5e0** (`1f158eb5e014e1d9f50be76f066b8495cbb41fda`). It has the same tree as fc6a89d796 and the same parents (967f0dbebb, 0836dfdf4f). It's #7395's PR head now (`upstream/pr/7395`) and fork `upkeep/7395-split-0836dfdf4f-m`.
- **Row 1 merges 1f158eb5e0, not fc6a89d796.** Its message stays "Merge #7395's merge of main into …".
- **If you are cloud-2 and still have `local/prm111b-8939` (a02eea3055):** the new row 1's tree must equal a02eea3055's tree (`git rev-parse <new>^{tree} a02eea3055^{tree}`). If it doesn't, stop and report both.
- Fetch #8939's head from `upstream` (`+refs/pull/8939/head`). Stop if it isn't 13c1955909.

## 2. The count check, corrected

CI's `CONFIGURE` flags, `-DBUILD_TESTING=ON`, without `-DANYPS5_ENABLE_SPIRV_TOOLS=ON` (with it, each is +12). Main 0836dfdf4f gives 3075, and 1f158eb5e0 gives 3084.

| row | PR | expected `Total Tests` |
|---|---|---:|
| 1 | #8939 | **3089** |

**The json-v1 check:**
- No name appears twice.
- Every test of the row's base ref is there, with the same `command` and `properties`; `backtrace` is ignored.
- **The names not on `main`** must be exactly #7395's 9 plus the PR's stack's own, as PRM-111b §2 lists them for row 1.
- **Against the base ref,** the row adds only its PR's new names.
- **Any other difference is a stop:** a missing or extra name, a changed command or property, or a total off the table. Report the names.

## Push

Row 1 to `upkeep/8939-split-0836dfdf4f` on the fork (it doesn't exist yet), only after its checks pass. Nothing else is pushed, no PR branch is touched, and nothing is posted.

## Result

`results/cloud-<n>/PRM-111c.md`, in PRM-111b's row format. For row 1, give:
- the base ref used;
- the new SHA;
- the `FormatVersion` lines (old / base / merge);
- `Total Tests`;
- the names not on `main` and the names added over the base ref.

## When you finish

After you mark this task done, write a `done:` line in the task file, as the board's README says, then claim the next open task (priority high first). If none is open, restart the board watcher as a background command before you end your turn.

claimed-by: cloud-4 2026-11-01T01:59:32Z
