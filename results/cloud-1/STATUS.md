READY
RESULT:
- Last finished: PRM-029 (high), results/cloud-1/PRM-029.md.
  - CI Linux mirror on #6012's palette fix 84cae0bda0: configure, registration and build pass; ctest 1225/1231.
  - The 6 failures are all known baselines: four `guest_datagram_*` and `guest_tcp` (no IPv6), and `agc_driver_preparation_scope_settle`.
  - `check_skips.py` flags `guest_sce_net`, which skips because the host has no IPv6 loopback.
  - `agc_performance_overlay(_statistics)` passed 20/20, with identical SDR, HDR10 and full-panel lines in every run.
- Before that: PRM-028 (`pm/6636-snapshot-writes` 6e5b5bdf4), BF-040.
