cmake_minimum_required(VERSION 3.16)
project(tests)

include(FetchContent)
include(CTest)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY "https://github.com/google/googletest.git"
    GIT_TAG         v1.16.0
)

FetchContent_MakeAvailable(googletest)

add_executable(tests tests.cpp)
target_link_libraries(tests PRIVATE gtest gtest_main)
target_include_directories(tests PRIVATE .)

add_test(NAME Canary COMMAND tests)