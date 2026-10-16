# Headless gameplay: simulation tests link this without a window or GPU.
add_library(microjump_gameplay STATIC follow_camera.cpp level.cpp lighting.cpp player.cpp simulation.cpp)
target_include_directories(microjump_gameplay PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(microjump_gameplay PUBLIC mj::engine)
mj_project_options(microjump_gameplay)
add_executable(microjump main.cpp debug_ui.cpp)
target_link_libraries(microjump PRIVATE microjump_gameplay mj_imgui)
mj_project_options(microjump)
set(mj_shader_outputs)
foreach(shader scene.vert scene.frag shadow.vert fullscreen.vert sky.frag tonemap.frag)
    set(output "${CMAKE_CURRENT_BINARY_DIR}/shaders/${shader}.spv")
    add_custom_command(OUTPUT "${output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        COMMAND "${MJ_GLSLC}" --target-env=vulkan1.3 "${PROJECT_SOURCE_DIR}/shaders/${shader}" -o "${output}"
        DEPENDS "${PROJECT_SOURCE_DIR}/shaders/${shader}" "${PROJECT_SOURCE_DIR}/shaders/frame.glsl" VERBATIM)
    list(APPEND mj_shader_outputs "${output}")
endforeach()
add_custom_target(microjump_shaders ALL DEPENDS ${mj_shader_outputs})
add_dependencies(microjump microjump_shaders)
install(FILES ${mj_shader_outputs} DESTINATION shaders COMPONENT Runtime)
if(WIN32)
    add_custom_command(TARGET microjump POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3-shared> $<TARGET_FILE_DIR:microjump>)
endif()
set_target_properties(microjump PROPERTIES INSTALL_RPATH "$ORIGIN")
install(TARGETS microjump RUNTIME DESTINATION . COMPONENT Runtime)
install(FILES $<TARGET_FILE:SDL3::SDL3-shared> DESTINATION . COMPONENT Runtime)
if(UNIX)
    install(FILES $<TARGET_SONAME_FILE:SDL3::SDL3-shared> DESTINATION . COMPONENT Runtime)
endif()
