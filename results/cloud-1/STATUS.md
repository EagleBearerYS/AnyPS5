READY
RESULT:
- Last task: PM-049 (high), done. Result: results/cloud-1/PM-049.md. Fix: fork fix/agc-refused-import-retention 1c78ff4ec93e (one commit on upstream main 5d9dcc42d).
- Refusals keep nothing alive on main. 14 dispatch and draw series ran 8000 builds each, with up to 964,500 refusals, on both arms. Every category is flat, and RSS moves by at most +12 kB.
- Only a sparse heap grows (a reserved range whose committed window slides upward):
  - the live sparse mirror follows the commit high-water mark;
  - the pool's large tier keeps every superseded mirror up to its 512 MiB budget.
  - 64 MiB reservation: 143 → 677 MB. 256 MiB: 145 → 949 MB.
- The fix frees sparse mirrors instead of pooling them: 268 MB and 467 MB, large tier 0. The high-water mirror remains; a re-anchoring fix is proposed in the result.
- Red: "AGC graphics: the buffer pool holds 3 of the 3 sparse heap mirrors its regrowth superseded". Green: "Graphics validation tests passed".
- Checks: CI configure, registration and build exit 0; ^agc 606/606; agc_driver_graphics x10 passed; conventions passed. Merge-tree is clean with #6457 and #6479.
- Addenda:
  - half the import budget only moves bytes held for the GPU (+8 MiB of mirrors per halving);
  - the [descriptors] dedicated-pool figure counts creations, not live pools;
  - #6457's refusal map stays at 387-814 entries.
- perf/agc-async-settle b62a0572e7 is on neither remote, so I used d528b4476b. Its import changes are Windows-only, and its numbers equal main's.
