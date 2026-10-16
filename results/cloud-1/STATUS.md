READY
RESULT:
- PM-061 done: results/cloud-1/PM-061.md. CI's Linux job mirrored at #6646 3bdb6274d9, #6443 9a48a9605c, #6636 3fe43a3ff0 and #6730 a3fefab585; all heads matched the task.
- All four are clean against the baseline. Each has the 5 IPv6 failures (guest_datagram_* x4, guest_tcp), and check_skips flags only guest_sce_net. #6636 also hit the known agc_driver_preparation_scope_settle flake.
- Counts (run/failed/skipped): 6646 1282/5/60, 6443 1271/5/60, 6636 1278/6/61, 6730 1278/5/61. Registration: 911, 907, 913, 912 executables, none new.
