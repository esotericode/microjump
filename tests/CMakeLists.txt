add_executable(microjump_tests foundation_tests.cpp scene_tests.cpp collision_tests.cpp
    movement_tests.cpp replay_tests.cpp)
target_link_libraries(microjump_tests PRIVATE microjump_gameplay doctest::doctest)
target_compile_definitions(microjump_tests PRIVATE
    MJ_TEST_REPLAYS="${CMAKE_CURRENT_SOURCE_DIR}/replays")
mj_project_options(microjump_tests)
add_executable(microjump_capture_check capture_check.cpp)
target_link_libraries(microjump_capture_check PRIVATE mj::engine)
target_include_directories(microjump_capture_check SYSTEM PRIVATE ${stb_SOURCE_DIR})
mj_project_options(microjump_capture_check)
if(WIN32)
    add_custom_command(TARGET microjump_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3-shared> $<TARGET_FILE_DIR:microjump_tests>)
    add_custom_command(TARGET microjump_capture_check POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3-shared> $<TARGET_FILE_DIR:microjump_capture_check>)
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
    set(captures "${CMAKE_CURRENT_BINARY_DIR}/captures")
    add_test(NAME render_capture COMMAND microjump --capture-frame 12 --capture-out "${captures}/scene.png")
    add_test(NAME render_camera COMMAND microjump --camera-test --capture-frame 36 --capture-out "${captures}/camera.png")
    add_test(NAME render_resize COMMAND microjump --resize-test --capture-frame 16 --capture-out "${captures}/resized.png")
    # Recorded real input through the full game: replay path, player rendering, zero validation errors.
    add_test(NAME render_replay COMMAND microjump --replay "${CMAKE_CURRENT_SOURCE_DIR}/replays/m2-input-smoke.mjr"
        --capture-frame 160 --capture-out "${captures}/replay.png")
    add_test(NAME replay_completes COMMAND microjump --replay "${CMAKE_CURRENT_SOURCE_DIR}/replays/m2-input-smoke.mjr")
    set_tests_properties(replay_completes PROPERTIES PASS_REGULAR_EXPRESSION "Replay finished: tick 439,.*grounded.*validation errors: 0")
    add_test(NAME capture_pixels COMMAND microjump_capture_check "${captures}/scene.png" "${captures}/camera.png"
        "${captures}/resized.png" "${captures}/replay.png")
    set_tests_properties(render_capture render_camera render_resize render_replay replay_completes capture_pixels
        PROPERTIES TIMEOUT 60 LABELS "display;vulkan")
    set_tests_properties(capture_pixels PROPERTIES DEPENDS "render_capture;render_camera;render_resize;render_replay")
    if(MJ_SANITIZERS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_tests_properties(vulkan_startup render_capture render_camera render_resize render_replay replay_completes
            capture_pixels PROPERTIES ENVIRONMENT
            "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp;UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")
    endif()
endif()
