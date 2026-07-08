cmake_minimum_required(VERSION 3.16)
project(DemonSlayer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use system raylib if present, otherwise download and build it.
find_package(raylib 5.0 QUIET)
if(NOT raylib_FOUND)
    include(FetchContent)
    set(FETCHCONTENT_QUIET OFF)
    FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG        5.5
        GIT_SHALLOW    TRUE
    )
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(raylib)
endif()

file(GLOB SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_executable(demonslayer ${SRC})
target_link_libraries(demonslayer PRIVATE raylib)

if(WIN32)
    target_link_libraries(demonslayer PRIVATE winmm gdi32 opengl32)
endif()
if(APPLE)
    target_link_libraries(demonslayer PRIVATE "-framework Cocoa" "-framework IOKit" "-framework OpenGL")
endif()
