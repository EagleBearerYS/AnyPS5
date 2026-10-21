READY

RESULT:
- finished BF-098 (results/cloud-1/BF-098.md): branch perf/agc-shared-bda-fault-buffers at 2545cdfbd (on upstream main a07e985c8, commits 79b229ad7 and 2545cdfbd)
- BDA fault buffers: 97.7% of "shader data" before; 7.6% and 8.5% after. Village bench --cost 20 host-visible peak: 214.4 and 224.4 MiB before; 30.4 and 28.4 MiB after
- reds:
  - commit 1: "256 idle cached BdaResources hold 256 fault slots in 13 chunks"
  - commit 2: "64 report-only BdaResources hold 4 fault chunks with 24620-byte slots", plus a plumbing red and a proof red
- tests:
  - graphics x10: 10/10
  - BDA/fault set (31 tests, incl. agc_shader_srt_read_patching) x10: 290 passed, 0 failed, 2 skipped
  - CI mirror base: 1775/1782; head: 1776/1782. The 6 network/socket failures occur on both sides; base also had a sgpr_pattern_fill_keys timing flake
- overlaps: the branch adds no conflict with #7817, #8145, #6412, #7465, #7392, #7942 or #6636. #6412 keeps the 4 conflicts it already has with main
