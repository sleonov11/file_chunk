cmake_minimum_required(VERSION 3.16)
project(FileCollector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FILECOLLECTOR_BUILD_TESTS "Build unit tests" ON)
option(FILECOLLECTOR_SANITIZE    "Enable ASan+UBSan" OFF)

add_library(filecollector STATIC FileCollector.cpp)
target_include_directories(filecollector PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(filecollector PRIVATE -Wall -Wextra -Wpedantic)

if(FILECOLLECTOR_SANITIZE)
    target_compile_options(filecollector PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(filecollector PUBLIC -fsanitize=address,undefined)
endif()

if(FILECOLLECTOR_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    add_executable(filecollector_tests tests/FileCollectorTests.cpp)
    target_link_libraries(filecollector_tests PRIVATE
        filecollector
        GTest::gtest_main
    )
    target_compile_options(filecollector_tests PRIVATE -Wall -Wextra -Wpedantic)

    include(GoogleTest)
    gtest_discover_tests(filecollector_tests)
endif()