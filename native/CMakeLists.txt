cmake_minimum_required(VERSION 3.24)
project(dc_native LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)   # no C++20 modules: avoids a Ninja rule-name problem with llvm-mingw + CMake 4

set(GODOTCPP_DISABLE_EXCEPTIONS OFF CACHE BOOL "dccore reports errors with C++ exceptions" FORCE)
# godot-cpp (tag 10.0.0-stable, default API 4.7; matches the installed Godot 4.7.2)
add_subdirectory(godot-cpp)
# engine-independent game library
add_subdirectory(dccore)

# GDExtension that exposes dccore to Godot
add_library(dcgodot SHARED
    gdext/src/register_types.cpp
    gdext/src/dc_core.cpp
    gdext/src/dc_models.cpp
    gdext/src/dc_character.cpp
    gdext/src/dc_dungeon.cpp
    gdext/src/dc_walker.cpp
    gdext/src/dc_town.cpp
    gdext/src/dc_townwalk.cpp
    gdext/src/dc_townedit.cpp
    gdext/src/dc_townfish.cpp
    gdext/src/dc_opening.cpp
    gdext/src/dc_sound.cpp)
target_include_directories(dcgodot PRIVATE gdext/src)
target_link_libraries(dcgodot PRIVATE godot-cpp dccore)
get_target_property(GODOTCPP_SUFFIX godot-cpp GODOTCPP_SUFFIX)
set(DC_OUT "${CMAKE_CURRENT_SOURCE_DIR}/godot/bin")
set_target_properties(dcgodot PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    LIBRARY_OUTPUT_DIRECTORY "$<1:${DC_OUT}>"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${DC_OUT}>"
    PREFIX "lib"
    OUTPUT_NAME "dcgodot${GODOTCPP_SUFFIX}")
