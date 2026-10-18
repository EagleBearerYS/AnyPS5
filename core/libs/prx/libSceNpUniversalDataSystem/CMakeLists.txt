add_library(libSceNpUniversalDataSystem SHARED EXCLUDE_FROM_ALL
        ${CMAKE_CURRENT_SOURCE_DIR}/src/System.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Context.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Handle.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Event.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/EventPropertyObject.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/EventPropertyArray.cpp
)

target_include_directories(libSceNpUniversalDataSystem PRIVATE
        ${LIBS_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(libSceNpUniversalDataSystem PRIVATE libc)
configure_windows_unwind(libSceNpUniversalDataSystem)

if(BUILD_TESTING)
    add_test_executable(guest_np_trophy_data_tests ${LIBS_INCLUDE_DIR}/tests/GuestNpTrophyData.cpp)
    target_include_directories(guest_np_trophy_data_tests PRIVATE ${LIBS_INCLUDE_DIR})
    target_link_libraries(guest_np_trophy_data_tests PRIVATE libSceNpUniversalDataSystem libSceNpTrophy2 libc)
    configure_windows_unwind(guest_np_trophy_data_tests)
    add_test(NAME guest_np_trophy_data COMMAND guest_np_trophy_data_tests)
    if(WIN32)
        set_tests_properties(guest_np_trophy_data PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libSceNpUniversalDataSystem>")
    endif()
endif()
