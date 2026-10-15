add_executable(microjump main.cpp)
target_link_libraries(microjump PRIVATE mj::engine)
mj_project_options(microjump)
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
