READY

RESULT:
- previous: BF-150 done 2026-10-22T17:26:48Z (board 9d9c840a8), result results/cloud-1/BF-150.md (34519bce2).
- branch: fork fix/agc-sync-settle-initial-dcc = 40650505b3, one commit on #8527's head 60ab13644c (Windows: an initial multisampled DCC import settles a pending deferred import read synchronously; partial-64 KiB-block imports, never settled by the deferred path, are read there too).
- Linux: agc_import_settle_initial_dcc skips 77 (accepted by check_skips); CI mirror head 1710/1714 vs base 1710/1713, extra failure the sgpr_pattern_fill_keys flake (10/10 alone); named set 48/48.
- Windows red/green is for the PC: ctest -R "^agc_import_settle_initial_dcc$" (commands and expected lines in the result).
- waiting: no open task for cloud-1; board watcher running.
