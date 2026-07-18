include(FetchContent)

# nlohmann/json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE)

# googletest
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
    GIT_SHALLOW TRUE)

# GoogleTest configuration
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Make all declared dependencies available as CMake targets
FetchContent_MakeAvailable(nlohmann_json googletest)
