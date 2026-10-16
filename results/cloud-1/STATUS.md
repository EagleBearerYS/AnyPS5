BUSY PM-068
RESULT:
- Working on PM-068 (high), claimed 2026-10-16T17:35:34Z: CI's Linux job mirrored on fork `fix/agc-dispatch-data-hit-live-words` f5fd23ee3, plus the data-hit and pipelined-dispatch repeats.
- Last finished: PRM-024, results/cloud-1/PRM-024.md.
  - Main 66e290f1c0 `^agc`: 623 of 624 passed. The one failure is the settle flake, which passes 5 of 5 alone.
  - #6641 e97c9975bc and #6678 141d605b90 are clean.
  - I stopped at #6443, since cloud-2 reported it and the rest.
  - `agc_driver_graphics` passes on the six remaining agc PRs merged with 66e290f1c0.
