include(FetchContent)
find_package(Vulkan 1.3 REQUIRED)

# Only charter-approved libraries. Archive hashes also protect against mutable tags.
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "" FORCE) # Audio is outside the prototype until M4.
FetchContent_Declare(SDL3
    URL https://codeload.github.com/libsdl-org/SDL/tar.gz/fa2c02bb6e21974a89ea9824bc53c9932abe5f9c
    URL_HASH SHA256=d246967187a4f37cc850608dbaf5cff9d1df4d731259903d25539755cdfcc657)
FetchContent_Declare(volk
    URL https://codeload.github.com/zeux/volk/tar.gz/d979819fd03de3a7606d6e23d6ff4968942599da
    URL_HASH SHA256=ff6facfca90161325a1f817c08c7edb6f892bd3bf4745449b735d7e13585ec4c)
FetchContent_MakeAvailable(SDL3 volk)
if(MJ_BUILD_TESTS)
    set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(doctest
        URL https://codeload.github.com/doctest/doctest/tar.gz/1da23a3e8119ec5cce4f9388e91b065e20bf06f5
        URL_HASH SHA256=3ffeeefdd63fa4978a9a835b6007dcbe8d28de45411665285516b3490ca253df)
    FetchContent_MakeAvailable(doctest)
endif()
