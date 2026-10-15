add_executable(microjump_tests foundation_tests.cpp)
target_link_libraries(microjump_tests PRIVATE mj::engine doctest::doctest)
mj_project_options(microjump_tests)
if(WIN32)
    add_custom_command(TARGET microjump_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3-shared> $<TARGET_FILE_DIR:microjump_tests>)
endif()
add_test(NAME foundation COMMAND microjump_tests)
add_test(NAME cli_help COMMAND microjump --help)
add_test(NAME cli_rejects_unknown_argument COMMAND microjump --not-an-option)
set_tests_properties(cli_rejects_unknown_argument PROPERTIES WILL_FAIL TRUE)
set_tests_properties(foundation cli_help cli_rejects_unknown_argument PROPERTIES
    ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")
if(MJ_SMOKE_TESTS)
    add_test(NAME vulkan_startup COMMAND microjump --smoke-test)
    set_tests_properties(vulkan_startup PROPERTIES TIMEOUT 30 LABELS "display;vulkan")
    if(MJ_SANITIZERS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_tests_properties(vulkan_startup PROPERTIES ENVIRONMENT
            "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp;UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")
    endif()
endif()
