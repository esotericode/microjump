add_executable(microjump_tests foundation_tests.cpp scene_tests.cpp collision_tests.cpp
    movement_tests.cpp move_verbs_tests.cpp momentum_tests.cpp frame_pacing_tests.cpp replay_tests.cpp lighting_tests.cpp level_tests.cpp gamepad_tests.cpp
    collectible_tests.cpp collectible_content_tests.cpp character_tests.cpp particle_tests.cpp lantern_isles_tests.cpp surface_tests.cpp)
target_link_libraries(microjump_tests PRIVATE microjump_gameplay doctest::doctest)
target_compile_definitions(microjump_tests PRIVATE
    MJ_TEST_REPLAYS="${CMAKE_CURRENT_SOURCE_DIR}/replays"
    MJ_TEST_LEVELS="${PROJECT_SOURCE_DIR}/assets/levels")
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
add_test(NAME cli_rejects_replay_overflow COMMAND microjump --replay "${CMAKE_CURRENT_SOURCE_DIR}/replays/overflow.mjr")
add_test(NAME cli_rejects_missing_level COMMAND microjump --level "${CMAKE_CURRENT_BINARY_DIR}/no-level.json")
set_tests_properties(cli_rejects_unknown_argument cli_rejects_replay_overflow cli_rejects_missing_level PROPERTIES WILL_FAIL TRUE)
# A version 3 recording names its level; replaying it on another level stops before any window.
add_test(NAME cli_rejects_replay_level_mismatch COMMAND microjump
    --level "${PROJECT_SOURCE_DIR}/assets/levels/movement-yard.json"
    --replay "${CMAKE_CURRENT_SOURCE_DIR}/replays/m4-momentum-chain.mjr")
set_tests_properties(cli_rejects_replay_level_mismatch PROPERTIES
    PASS_REGULAR_EXPRESSION "Replay was recorded on level 'microjump-playground' but 'm2-yard' is loaded")
set_tests_properties(foundation cli_help cli_rejects_unknown_argument cli_rejects_replay_overflow cli_rejects_missing_level cli_rejects_replay_level_mismatch PROPERTIES
    ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")
if(MJ_SMOKE_TESTS)
    add_executable(microjump_particle_render_tests particle_render_tests.cpp)
    target_link_libraries(microjump_particle_render_tests PRIVATE mj::engine)
    target_include_directories(microjump_particle_render_tests SYSTEM PRIVATE ${stb_SOURCE_DIR})
    mj_project_options(microjump_particle_render_tests)
    set_target_properties(microjump_particle_render_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/game")
    add_dependencies(microjump_particle_render_tests microjump_shaders)
    add_test(NAME particle_render COMMAND microjump_particle_render_tests "${CMAKE_CURRENT_BINARY_DIR}/captures/particles")
    set_tests_properties(particle_render PROPERTIES TIMEOUT 90 LABELS "display;vulkan;particles")
    add_executable(microjump_gamepad_smoke gamepad_window_tests.cpp "${PROJECT_SOURCE_DIR}/game/debug_ui.cpp")
    target_link_libraries(microjump_gamepad_smoke PRIVATE microjump_gameplay mj_imgui doctest::doctest)
    mj_project_options(microjump_gamepad_smoke)
    set_target_properties(microjump_gamepad_smoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/game")
    add_dependencies(microjump_gamepad_smoke microjump_shaders)
    add_test(NAME gamepad_window COMMAND microjump_gamepad_smoke)
    set_tests_properties(gamepad_window PROPERTIES TIMEOUT 90 LABELS "display;vulkan;gamepad")
    add_test(NAME vulkan_startup COMMAND microjump --smoke-test)
    set_tests_properties(vulkan_startup PROPERTIES TIMEOUT 30 LABELS "display;vulkan")
    set(captures "${CMAKE_CURRENT_BINARY_DIR}/captures")
    set(yard "${PROJECT_SOURCE_DIR}/assets/levels/movement-yard.json")
    add_test(NAME render_capture COMMAND microjump --level "${yard}" --capture-frame 12 --capture-out "${captures}/scene.png")
    add_test(NAME render_camera COMMAND microjump --level "${yard}" --camera-test --capture-frame 36 --capture-out "${captures}/camera.png")
    add_test(NAME render_resize COMMAND microjump --level "${yard}" --resize-test --capture-frame 16 --capture-out "${captures}/resized.png")
    # Recorded real input through the full game: replay path, player rendering, zero validation errors.
    add_test(NAME render_replay COMMAND microjump --level "${yard}" --replay "${CMAKE_CURRENT_SOURCE_DIR}/replays/m2-input-smoke.mjr"
        --capture-tick 320 --capture-out "${captures}/replay.png") # Tick-based: any frame rate.
    add_test(NAME replay_completes COMMAND ${CMAKE_COMMAND}
        "-DLEVEL=${yard}"
        "-DGAME=$<TARGET_FILE:microjump>"
        "-DREPLAY=${CMAKE_CURRENT_SOURCE_DIR}/replays/m2-input-smoke.mjr"
        "-DEXPECTED=Replay finished: tick 439,.*grounded"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_replay.cmake")
    add_test(NAME replay_empty COMMAND ${CMAKE_COMMAND}
        "-DLEVEL=${yard}"
        "-DGAME=$<TARGET_FILE:microjump>"
        "-DREPLAY=${CMAKE_CURRENT_SOURCE_DIR}/replays/empty.mjr"
        "-DEXPECTED=Replay finished: tick 0,"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_replay.cmake")
    add_test(NAME capture_exact_tick COMMAND ${CMAKE_COMMAND}
        "-DLEVEL=${yard}"
        "-DGAME=$<TARGET_FILE:microjump>"
        "-DREPLAY=${CMAKE_CURRENT_SOURCE_DIR}/replays/m2-input-smoke.mjr"
        "-DCAPTURE_TICK=1" "-DCAPTURE_OUT=${captures}/exact-tick.png"
        "-DEXPECTED=Capture state: tick 1,"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_replay.cmake")
    # M3 lighting: afternoon vs night at spawn, and the pavilion's colored lights at night.
    add_test(NAME render_day COMMAND microjump --level "${yard}" --hide-overlay --time-of-day 14.5
        --capture-frame 30 --capture-out "${captures}/day.png")
    add_test(NAME render_night COMMAND microjump --level "${yard}" --hide-overlay --time-of-day 22 --shadow-quality low
        --capture-frame 30 --capture-out "${captures}/night.png")
    add_test(NAME render_pavilion COMMAND microjump --level "${yard}" --replay "${CMAKE_CURRENT_SOURCE_DIR}/replays/m3-pavilion-tour.mjr"
        --hide-overlay --time-of-day 22 --shadow-quality high --capture-tick 1380 --capture-out "${captures}/pavilion.png")
    add_test(NAME lighting_pixels COMMAND microjump_capture_check --lighting "${captures}/day.png"
        "${captures}/night.png" "${captures}/pavilion.png")
    set_tests_properties(lighting_pixels PROPERTIES DEPENDS "render_day;render_night;render_pavilion")
    add_test(NAME replay_m4_moves COMMAND ${CMAKE_COMMAND}
        "-DGAME=$<TARGET_FILE:microjump>"
        "-DREPLAY=${CMAKE_CURRENT_SOURCE_DIR}/replays/m4-moves-tour.mjr"
        "-DEXPECTED=Capture state: tick 2899,.*grounded, checkpoint 10"
        "-DCAPTURE_TICK=2899" "-DCAPTURE_OUT=${captures}/m4-moves.png"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_replay.cmake")
    add_test(NAME render_playground COMMAND microjump --capture-tick 60 --capture-out "${captures}/playground.png")
    add_test(NAME render_isles COMMAND microjump --level "${PROJECT_SOURCE_DIR}/assets/levels/lantern-isles.json"
        --capture-tick 60 --capture-out "${captures}/isles.png")
    add_test(NAME isles_pixels COMMAND microjump_capture_check --scene "${captures}/isles.png")
    set_tests_properties(isles_pixels PROPERTIES DEPENDS render_isles)
    # Through the playground's gate to the isles and back: mesh swaps under validation.
    add_test(NAME travel_gates COMMAND ${CMAKE_COMMAND}
        "-DGAME=$<TARGET_FILE:microjump>" "-DEXTRA=--travel-test"
        "-DEXPECTED=Traveled through 'To the Lantern Isles' to lantern-isles.*Traveled through 'Back to the playground' to microjump-playground"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_replay.cmake")
    # The pier's long leap through the whole game, landing on Lantern Isle with its spark.
    add_test(NAME replay_isles_leap COMMAND ${CMAKE_COMMAND}
        "-DGAME=$<TARGET_FILE:microjump>"
        "-DLEVEL=${PROJECT_SOURCE_DIR}/assets/levels/lantern-isles.json"
        "-DREPLAY=${CMAKE_CURRENT_SOURCE_DIR}/replays/isles-long-leap.mjr"
        "-DEXPECTED=Replay finished: tick 628, feet [(][^,]*, -3[.]99[0-9]*, [^)]*[)], state Run, grounded, checkpoint 5, sparks 1/10"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_replay.cmake")
    add_test(NAME render_spark_pickup COMMAND ${CMAKE_COMMAND}
        "-DGAME=$<TARGET_FILE:microjump>"
        "-DREPLAY=${CMAKE_CURRENT_SOURCE_DIR}/replays/spark-invitation.mjr"
        "-DEXPECTED=Capture state: tick 140,.*sparks 1/16"
        "-DCAPTURE_TICK=140" "-DCAPTURE_OUT=${captures}/spark-pickup.png"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_replay.cmake")
    add_test(NAME replay_spark_invitation COMMAND ${CMAKE_COMMAND}
        "-DGAME=$<TARGET_FILE:microjump>"
        "-DREPLAY=${CMAKE_CURRENT_SOURCE_DIR}/replays/spark-invitation.mjr"
        "-DEXPECTED=Replay finished: tick 170,.*sparks 1/16"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_replay.cmake")
    add_test(NAME playground_pixels COMMAND microjump_capture_check --scene "${captures}/playground.png")
    set_tests_properties(playground_pixels PROPERTIES DEPENDS render_playground)
    add_test(NAME capture_pixels COMMAND microjump_capture_check "${captures}/scene.png" "${captures}/camera.png"
        "${captures}/resized.png" "${captures}/replay.png")
    set_tests_properties(render_capture render_camera render_resize render_replay replay_completes replay_empty capture_exact_tick capture_pixels
        render_day render_night render_pavilion lighting_pixels render_playground playground_pixels replay_m4_moves
        render_spark_pickup replay_spark_invitation render_isles isles_pixels travel_gates replay_isles_leap PROPERTIES TIMEOUT 90 LABELS "display;vulkan")
    set_tests_properties(capture_pixels PROPERTIES DEPENDS "render_capture;render_camera;render_resize;render_replay")
    if(MJ_SANITIZERS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_tests_properties(gamepad_window vulkan_startup render_capture render_camera render_resize render_replay replay_completes replay_empty capture_exact_tick
            capture_pixels render_day render_night render_pavilion lighting_pixels render_playground playground_pixels replay_m4_moves
            render_spark_pickup replay_spark_invitation particle_render render_isles isles_pixels travel_gates replay_isles_leap PROPERTIES ENVIRONMENT
            "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp;UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")
    endif()
endif()
