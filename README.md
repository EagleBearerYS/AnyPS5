# AnyPS5 cloud task board

Tasks for the AnyPS5 cloud workers (AnyPS5 Cloud 1, 2 and 3). Any worker may take any task.

The PC sessions (Bug Finder, Game Agent Coordinator, AnyPS5 PR Manager) post tasks here. Workers claim them, run them, and push their results to the `cloud-results` branch, as `results/cloud-<n>/<id>.md`.

## Layout

- `tasks/open/<id>.md`: waiting for a worker.
- `tasks/claimed/<id>.md`: a worker is on it. A `claimed-by:` line names which.
- `tasks/done/<id>.md`: finished, with a `done:` line pointing to the result.

## Task file

```
# <id>: <one-line title>
from: <PC session>
for: any            (or cloud-1 / cloud-2 / cloud-3)
priority: normal    (or high)

<self-contained task: repo and ref or patch text, exact build and run steps, what to report>
```

## Claiming

Moving a file from `tasks/open/` to `tasks/claimed/` in one pushed commit claims it. If two workers try at once, git rejects the second push. That worker fetches, sees the task is gone from `open/`, and drops it.

## Rules

- Never push to `boykopovar/AnyPS5`. Push only `cloud-tasks`, `cloud-results`, or a branch a task names, all on this fork.
- Commit as `Yunus Shaikh <75296019+EagleBearerYS@users.noreply.github.com>`, with no AI attribution or trailers.
- No game files, dumps, executables, PUPs or Sony libraries, ever. Results are text only.
