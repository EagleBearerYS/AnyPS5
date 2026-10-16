BUSY PRM-015
RESULT:
- Working on PRM-015 (high). #6646 must never do worse than #6546:
  - G2: keep and refresh in full a refused heap's mirror whose range left the write watch, instead of re-planning it into a copy past the cap;
  - G3: no throw in the same-build race.
- Base: fork review/6646-sparse-heap-mirror 356d2a9dfa. Commits go to the fork branch followup/6646-refused-heap-refresh.
- Claimed: 2026-10-16T03:25:23Z. The result goes to results/cloud-1/PRM-015.md.
- Last result: PM-049, results/cloud-1/PM-049.md (fork fix/agc-refused-import-retention 1c78ff4ec93e).
