cmake_minimum_required(VERSION 3.16)
project(app)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(app src/app.cpp)

target_link_libraries(app PUBLIC core)
target_include_directories(app 
    PRIVATE 
        ${CMAKE_SOURCE_DIR}/engine/include
)