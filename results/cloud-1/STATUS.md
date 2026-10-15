BUSY PM-031 (claimed 2026-10-15T04:07:15Z)
RESULT:
- Last task: GAC-016, done: results/cloud-1/GAC-016.md.
  - test/village-fast-main 00582f7a45 (fast-main with PM-027 in place of the switch helper) builds with CI's flags: configure, registration, build and libs exit 0, no compile error.
  - `^agc`: 2 of 584 fail, both GAC-010's sample experiments (agc_driver_graphics, agc_driver_covered_centroid_state), which pass with the three APS5_RE8_STRICT_* switches. Nothing else fails; the 41 skips are main's.
  - The 8 image-table and PM-027 tests pass.
  - test/village-fast-main-locallazy bf89905763: configure, registration, build and libs exit 0 (not tested, as asked).
  - PM-028 onto 00582f7a45 (scratch merge-tree, not resolved): one file, ResourceMaterializer.cpp, 2 logging-only hunks (TableCounters, reportTables). Both are mechanical; PM-028 needs no rework, because selected records return before its memo.
- Task before it: PRM-009, done: results/cloud-1/PRM-009.md.
  - Upstream main a077a88df5. All six PRs merge cleanly and build with CI's flags: #5703, #5815, #5966, #5967, #6012 at 67acc14910 (fast-forward), and #6146.
  - Named tests pass. `^agc` shows only the agc_driver_preparation_scope_settle parallel flake, which fails on main too and passes alone. The guest_datagram 0/4 and guest_sce_net skip match main (no IPv6).
  - All six merged together (0665355fef): clean. The full suite runs 1233 tests with 6 failures, all baseline, and the same 59 skips as main.
