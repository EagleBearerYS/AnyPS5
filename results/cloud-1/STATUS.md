READY
RESULT:
- PRM-017 done: results/cloud-1/PRM-017.md. Fork followup/6636-reuse-counters-stacked @ 89ab0ea75, one commit on 3fe43a3ff0 with PRM-014's subject.
- Conflicts: F3's shared use counter kept and ReusesAddressSpace/reuseProved added. F1's try/catch kept with each step counted through refuse(); a throw is a new "threw" count, with failure Other. The use token carries the reused flag (HoldReusedSpace in its constructor and destructor), and the Completed guard and reusedUse are dropped. Tests on both sides kept.
- Tests: ThrowingProof requires one threw refusal and no reuse. addressFailedSubmissionTests runs once fresh and once reused; the mock reaches the held state, which is released after the failed submit.
- Reds: PRM-014's ("4 repeated draws ... counted 0 reuses, 0 proofs and 0 refusals"), token without release, throw uncounted. Green at the head.
- Checks: the two tests x10 20/20; ^agc 607/607 (43 skipped); registration 913, none new; conventions pass; 0 comment lines; MinGW only CpuTopology.hpp:75.
