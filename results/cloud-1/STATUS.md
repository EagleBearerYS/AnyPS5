BUSY GAC-023 (claimed 2026-10-15T09:02:34Z)
RESULT:
- Working on: GAC-023 (a PR-ready fix so explicit ABI resolution defers a stage it can't prepare, Village stop 26; fork branch fix/agc-defer-unpreparable-abi-stages).
- Last task: GAC-020, done: results/cloud-1/GAC-020.md.
  - #6146 v4 (fork fix/agc-db-depth-expand-v4 6596a1d3a2) on Linux: build exit 0; registration "868 test executable(s), 0 built and not run as recorded, none new".
  - Expand tests --repeat until-fail:10: 2/2. ^agc: 558/558, 40 skipped.
  - Split cases: green at the head (1 expected 1, 15360/15360); red with 103316e33 reverted (0 expected 1, then 0/15360 or 3072/15360). Identical to GAC-015, so the settle copy changes nothing on Linux.
  - Per commit: a4137dc3f 2/2, cee29fde7 1/1, 103316e33 1/1. Conventions pass; 0 added comments.
- Task before it: BF-009, done: results/cloud-1/BF-009.md (fork perf/shader-direct-cpu-evaluable-images-followup 23d6cf684).
