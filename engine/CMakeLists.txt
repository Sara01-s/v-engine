cmake_minimum_required(VERSION 3.20)
project(v-engine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wpedantic)

include(FetchContent)

# GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW ON
)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Disable GLFW examples" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "Disable GLFW tests" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "Disable GLFW docs" FORCE)
FetchContent_MakeAvailable(glfw)

# GLM
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW ON
)
FetchContent_MakeAvailable(glm)

# Vulkan
find_package(Vulkan QUIET)
if(NOT Vulkan_FOUND)
    message(WARNING "Vulkan not found on system. Fetching Vulkan-Headers and Vulkan-Loader.")
    FetchContent_Declare(
        vulkan_headers
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
        GIT_TAG v1.3.309
        GIT_SHALLOW ON
    )
    FetchContent_Declare(
        vulkan_loader
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Loader.git
        GIT_TAG v1.3.309
        GIT_SHALLOW ON
    )
    FetchContent_MakeAvailable(vulkan_headers vulkan_loader)
endif()

# Shaderc y sus dependencias
find_library(SHADERC_LIBRARY NAMES shaderc_combined libshaderc_combined)
find_path(SHADERC_INCLUDE_DIR shaderc/shaderc.hpp)
find_library(GLSLANG_LIBRARY NAMES glslang libglslang)
find_library(SPIRV_TOOLS_LIBRARY NAMES SPIRV-Tools libSPIRV-Tools)
find_library(SPIRV_TOOLS_OPT_LIBRARY NAMES SPIRV-Tools-opt libSPIRV-Tools-opt)

if(SHADERC_LIBRARY AND SHADERC_INCLUDE_DIR AND GLSLANG_LIBRARY AND SPIRV_TOOLS_LIBRARY AND SPIRV_TOOLS_OPT_LIBRARY)
    message(STATUS "Found system shaderc: ${SHADERC_LIBRARY}")
    message(STATUS "Found system glslang: ${GLSLANG_LIBRARY}")
    message(STATUS "Found system SPIRV-Tools: ${SPIRV_TOOLS_LIBRARY}")
    message(STATUS "Found system SPIRV-Tools-opt: ${SPIRV_TOOLS_OPT_LIBRARY}")
    add_library(shaderc INTERFACE)
    target_include_directories(shaderc INTERFACE ${SHADERC_INCLUDE_DIR})
    target_link_libraries(shaderc INTERFACE ${SHADERC_LIBRARY} ${GLSLANG_LIBRARY} ${SPIRV_TOOLS_LIBRARY} ${SPIRV_TOOLS_OPT_LIBRARY})
else()
    message(STATUS "Shaderc or its dependencies not found on system. Fetching and building from source.")
    FetchContent_Declare(
        spirv-tools
        GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Tools.git
        GIT_TAG v2024.1
        GIT_SHALLOW ON
    )
    FetchContent_Declare(
        glslang
        GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
        GIT_TAG 13.0.0
        GIT_SHALLOW ON
    )
    FetchContent_Declare(
        shaderc
        GIT_REPOSITORY https://github.com/google/shaderc.git
        GIT_TAG v2025.1
        GIT_SHALLOW ON
    )
    set(SHADERC_ENABLE_TESTS OFF CACHE BOOL "Disable shaderc tests" FORCE)
    set(SHADERC_ENABLE_EXAMPLES OFF CACHE BOOL "Disable shaderc examples" FORCE)
    set(SHADERC_SKIP_INSTALL ON CACHE BOOL "Skip shaderc install" FORCE)
    FetchContent_MakeAvailable(spirv-tools glslang shaderc)
endif()

file(GLOB_RECURSE CORE_SOURCES "src/*.cpp" "src/*.c")
file(GLOB_RECURSE CORE_HEADERS "include/core/*.hpp" "include/core/*.h")

message(STATUS "CORE_SOURCES: ${CORE_SOURCES}")
message(STATUS "CORE_HEADERS: ${CORE_HEADERS}")

add_library(core STATIC ${CORE_SOURCES} ${CORE_HEADERS})
target_include_directories(core PUBLIC include)

# Vincular dependencias
if(Vulkan_FOUND)
    target_link_libraries(core PUBLIC Vulkan::Vulkan)
else()
    target_include_directories(core PUBLIC ${vulkan_headers_SOURCE_DIR}/include)
    target_link_libraries(core PUBLIC vulkan)
endif()
target_link_libraries(core PUBLIC glfw glm)

# Vincular shaderc y sus dependencias
if(SHADERC_LIBRARY AND SHADERC_INCLUDE_DIR AND GLSLANG_LIBRARY AND SPIRV_TOOLS_LIBRARY AND SPIRV_TOOLS_OPT_LIBRARY)
    target_link_libraries(core PUBLIC
        ${SPIRV_TOOLS_LIBRARY}
        ${SPIRV_TOOLS_OPT_LIBRARY}
        ${GLSLANG_LIBRARY}
        ${SHADERC_LIBRARY}
    )
else()
    target_link_libraries(core PUBLIC
        SPIRV-Tools
        SPIRV-Tools-opt
        glslang
        shaderc_combined
    )
endif()

message(STATUS "Vulkan Headers Version: ${VulkanHeaders_VERSION}")