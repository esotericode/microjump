include(FetchContent)
find_package(Vulkan 1.3 REQUIRED)

# Only charter-approved libraries. Archive hashes also protect against mutable tags.
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_AUDIO ON CACHE BOOL "" FORCE) # Output only; the mixing and synthesis are ours.
if(MJ_SANITIZERS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Keep Xlib's process-global locale/resource caches visible to LeakSanitizer.
    # SDL's dlclose path otherwise unmaps their roots before the leak check.
    set(SDL_X11_SHARED OFF CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(SDL3
    URL https://codeload.github.com/libsdl-org/SDL/tar.gz/fa2c02bb6e21974a89ea9824bc53c9932abe5f9c
    URL_HASH SHA256=d246967187a4f37cc850608dbaf5cff9d1df4d731259903d25539755cdfcc657)
FetchContent_Declare(volk
    URL https://codeload.github.com/zeux/volk/tar.gz/d979819fd03de3a7606d6e23d6ff4968942599da
    URL_HASH SHA256=ff6facfca90161325a1f817c08c7edb6f892bd3bf4745449b735d7e13585ec4c)
FetchContent_MakeAvailable(SDL3 volk)
FetchContent_Declare(imgui
    URL https://codeload.github.com/ocornut/imgui/tar.gz/f5befd2d29e66809cd1110a152e375a7f1981f06
    URL_HASH SHA256=85f4ce357df05bcc331b587f01976f47fb55f19fadf477a7907289686bc3f4c8)
FetchContent_Declare(stb
    URL https://codeload.github.com/nothings/stb/tar.gz/2c980bb59875b0d32144a71867fbdebb2f77cd20
    URL_HASH SHA256=9a955b1b49a4410088a2e0ee2a9c057c3c907d0c1d75454144cb980aca0ba515)
FetchContent_MakeAvailable(imgui stb)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
set(JSON_SystemInclude ON CACHE BOOL "" FORCE)
FetchContent_Declare(nlohmann_json
    URL https://codeload.github.com/nlohmann/json/tar.gz/55f93686c01528224f448c19128836e7df245f72
    URL_HASH SHA256=67f4cdd9ca930c9c1e130af4a437c7fc98fab77a2846fc2d2a14b4943831f8ef)
FetchContent_MakeAvailable(nlohmann_json)
add_library(mj_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(mj_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(mj_imgui PRIVATE IMGUI_IMPL_VULKAN_USE_VOLK)
target_link_libraries(mj_imgui PUBLIC SDL3::SDL3 volk::volk)
add_library(mj_stb_write STATIC ${PROJECT_SOURCE_DIR}/third_party/stb_write.cpp)
target_include_directories(mj_stb_write SYSTEM PUBLIC ${stb_SOURCE_DIR})
find_program(MJ_GLSLC NAMES glslc HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
if(MJ_BUILD_TESTS)
    set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(doctest
        URL https://codeload.github.com/doctest/doctest/tar.gz/1da23a3e8119ec5cce4f9388e91b065e20bf06f5
        URL_HASH SHA256=3ffeeefdd63fa4978a9a835b6007dcbe8d28de45411665285516b3490ca253df)
    FetchContent_MakeAvailable(doctest)
endif()
