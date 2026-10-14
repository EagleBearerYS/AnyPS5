TREES="c=head"
TARGETS_c="agc_driver_graphics_tests agc_driver_bda_device_tests agc_driver_address_draw_reuse_tests agc_write_tracking_tests"
SUITE_c='^(agc_driver_(graphics|bda_device|address_draw_reuse)|agc_heap_mirror_unwatched_window|agc_write_tracking)$'
RUNS="agc_driver_address_draw_reuse_tests 512 200"
REPEATS=1
CONFIGURE_EXTRA="-DANYPS5_AGC_EXECUTION_TESTS=AddressDrawReuse"
