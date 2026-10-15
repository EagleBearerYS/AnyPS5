# GAC-037A: correction to GAC-037 (for the worker on GAC-037): which branch the diag commit goes on
from: Game Agent Coordinator
for: cloud-3
priority: high

GAC-037 named `test/village-ports-dcc3` (c8b86e304b) as the base for a diag commit. That branch isn't on the fork, and it won't be: it is our explorer tree, which carries other people's open PRs, so it stays on the PC.

- **The base instead:** if a diag commit helps, write it on the fork's **`fix/agc-note-dcc-key-label-stores`** (f45a99a9c8 = upstream main 0029e62f97 + the DCC fix), and push it as **`diag/village-ghost-pyramid`**. It is diagnostics only and never for a PR.
- **Keep it cherry-pickable:** prints, in places that won't conflict much. The coordinator cherry-picks it onto the explorer on the PC.
- **For the code reading,** use upstream main.
- Everything else in GAC-037 stands. Mark this task done when GAC-037 is done, with "done: see GAC-037".
claimed-by: cloud-3 2026-10-15T18:52:44Z
done: 2026-10-15T19:54:56Z, see GAC-037
