TREES="a=f40b6930f b=182ea1f6b c=head"
TARGETS_a="agc_driver_address_draw_reuse_tests"
TARGETS_b="agc_driver_address_draw_reuse_tests"
TARGETS_c="agc_driver_address_draw_reuse_tests agc_driver_graphics_tests agc_driver_bda_device_tests"
SUITE_a=""
SUITE_b=""
SUITE_c='^agc_driver_(graphics|bda_device|address_draw_reuse)$'
RUNS="agc_driver_address_draw_reuse_tests 512 200;agc_driver_address_draw_reuse_tests 6600 200;agc_driver_address_draw_reuse_tests 10800 200"
REPEATS=3
CONFIGURE_EXTRA="-DANYPS5_AGC_EXECUTION_TESTS=AddressDrawReuse"
