add_library(microjump_engine STATIC
    src/core/launch_options.cpp
    src/platform/window.cpp
    src/render/vulkan_context.cpp)
add_library(mj::engine ALIAS microjump_engine)
target_include_directories(microjump_engine PUBLIC include)
target_link_libraries(microjump_engine PUBLIC SDL3::SDL3 volk::volk)
target_compile_definitions(microjump_engine PUBLIC VK_NO_PROTOTYPES)
target_compile_definitions(microjump_engine PRIVATE $<$<CONFIG:Debug>:MJ_VULKAN_VALIDATION=1>)
mj_project_options(microjump_engine)
