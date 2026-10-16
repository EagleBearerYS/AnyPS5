READY
RESULT:
- Last finished: BF-035, results/cloud-1/BF-035.md.
- Fork branch `fix/agc-host-write-pending-storage` 99af947b1, on main 66e290f1c0.
- Fix: a host write into guest memory (APR, AIO, file and socket reads) now first stores the storage-image results pending over its range, and waits for every recorded or in-flight GPU write there.
  - It goes through a libc observer called at the start of every `GuestArena::HostWrite` scope, and the driver's `FlushBeforeHostWrite`.
- Test `agc_driver_host_write_pending_storage`, in three write-watch modes (userfaultfd user-mode-only, full userfaultfd, unwatched compare):
  - red 20 of 20 failing in each: a store recorded before the read zeroes the file data, and a 4 KiB read loses the image's texels around it;
  - green 20 of 20 in each.
- Cost: about 80 ns per host write with nothing to wait for.
- `^agc` 624 of 625 (the settle flake, 5 of 5 alone), and `guest|libc|kernel` 382 of 387 (IPv6), the same as main.
- Before that: PM-068 and PRM-024.
