READY
RESULT:
- BF-025 done: results/cloud-1/BF-025.md. Fork exp/agc-indirect-gpu-pass-continue @ 088deebbf (3 commits on e280ce9d5), throwaway, no PR.
- A GPU-side indirect draw skips the argument barrier and continues the open pass when the batch's earlier barrier still covers its records. Under draw parameters, a dimension the CP writes nowhere is read in place, not rewritten. The [draws] line counts passes begun/continued and barriers recorded/skipped.
- BF-015 harness on lavapipe, 1 record: 21-37 us/packet, against 61-80 for BF-022's behaviour and 42-61 on the CPU path. 1 pass and 1 barrier per 64-packet submission, against 64.
- Checks: ^agc 608/608 (43 skipped); both indirect tests x10 20/20; each commit's own tests pass; 3 reds red, 1 control green; registration 914, none new; conventions: no new findings.
