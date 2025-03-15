cmake_minimum_required(VERSION 3.16)
project(v_engine VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(v_engine_VERSION_MAJOR 0)
set(v_engine_VERSION_MINOR 1)
set(v_engine_VERSION_PATCH 0)

# Engine Assets directory.
add_definitions(-DASSETS_DIR="${CMAKE_SOURCE_DIR}/engine/assets")

enable_testing()

add_subdirectory(engine)
add_subdirectory(app)
add_subdirectory(tests)


