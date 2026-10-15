BUSY PM-035 (claimed 2026-10-15T07:08:21Z)
RESULT:
- Last task: PRM-010, done: results/cloud-1/PRM-010.md.
  - Upstream main d92a8dad39; overlay PR head 62022e9362 and #6194 8bb1300680, both as stated.
  - Tree A (main + 62022e9362): clean merge; CI configure, registration, build and libs exit 0. ctest: 1232 run, 6 failed (the 5 IPv6 baseline tests plus audio_out2_shutdown, a parallel-load flake that passed alone 20/20). check_skips: only guest_sce_net (IPv6 baseline). Conventions on the PR head pass.
  - Tree B (+ #6194): clean merge, builds; named tests 3/3, ^agc 579/579.
  - hidden cost: 0.32-0.44 ns per counted packet; shown 0.02-0.03 us per frame and 11.1-17.7 us per second.
  - With the old head 67acc14910, #6194 conflicts textually in ShaderResources.cpp (either order); 62022e9362 clears it.
- Task before it: GAC-017, done: results/cloud-1/GAC-017.md.
  - f146a9ade2 (fast-main with PM-027 + PM-028) builds with CI's flags: configure, registration ("890 test executable(s), 0 built and not run as recorded, none new"), build and libs exit 0, no error or warning.
  - `^agc`: 3 of 585 fail: GAC-010's two sample experiments and the known agc_driver_preparation_scope_settle flake (passed alone 20/20). 41 skips, main's.
  - The 9 image-table and PM-028 tests pass, including agc_shader_table_snapshot_reuse.
  - The merged [image-table] (10 s) line: no test prints it. It checks out statically (12 specifiers/12 arguments), with GCC -Wformat, and at runtime via a scratch clock shim, where every field printed.
  - 70312aa432 (-locallazy): configure, registration, build and libs exit 0 (not tested, as asked).
