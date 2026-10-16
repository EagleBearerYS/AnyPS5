READY
RESULT:
- PM-067 done: results/cloud-1/PM-067.md. #6683 merged head 418d5abb88 (unchanged), CI's Linux job mirrored: clean against the baseline.
- ctest 1294/1300 passed, 63 skipped: the 5 IPv6 failures (guest_datagram_* x4, guest_tcp) and agc_driver_graphics ("VOP2 opcode is not implemented", 0/5 alone; main's own failure, #6792, also on main e0e3ced1e). check_skips flags only guest_sce_net. Registration 917, none new.
- x5: request_serializer, agc_shader_memory, agc_driver_sample_order, agc_driver_sample_order_planning all 5/5; the two 2x-target sample-order tests skip on lavapipe (allowed reasons).
