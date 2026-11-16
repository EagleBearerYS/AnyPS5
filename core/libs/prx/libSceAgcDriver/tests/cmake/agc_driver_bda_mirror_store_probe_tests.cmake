if(BUILD_TESTING)
    add_test(NAME agc_driver_bda_mirror_store_probe_refused COMMAND agc_driver_bda_mirror_store_probe_tests --expect-refused)
    set_tests_properties(agc_driver_bda_mirror_store_probe_refused PROPERTIES ENVIRONMENT "ANYPS5_NO_SHADER_CACHE=1" SKIP_RETURN_CODE 77 TIMEOUT 60 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    if(WIN32)
        set_tests_properties(agc_driver_bda_mirror_store_probe_refused PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
    endif()
endif()
