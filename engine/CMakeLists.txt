cmake_minimum_required(VERSION 3.16)
project(core)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw
)
FetchContent_MakeAvailable(glfw)
######

# GLM
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm
)
FetchContent_MakeAvailable(glm)
######

# Vulkan
find_package(Vulkan REQUIRED)
########

file(GLOB_RECURSE CORE_SOURCES "src/*.cpp")
file(GLOB_RECURSE CORE_HEADERS "include/core/*.hpp")

message(STATUS "CORE_SOURCES: ${CORE_SOURCES}")
message(STATUS "CORE_HEADERS: ${CORE_HEADERS}")

add_library(core STATIC ${CORE_SOURCES} ${CORE_HEADERS})
target_include_directories(core PUBLIC include)
target_link_libraries(core PUBLIC Vulkan::Vulkan glfw glm)

message(STATUS "Vulkan Headers Version: ${VulkanHeaders_VERSION}")