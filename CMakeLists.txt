cmake_minimum_required(VERSION 3.20)
project(OpenBF1942Plus CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wconversion)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(EPOXY REQUIRED IMPORTED_TARGET epoxy)

# Jolt Physics (ADR 0004): pinned commit of tag v5.6.0, built with the engine.
include(FetchContent)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_OBJECT_STREAM OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
foreach(opt JPH_USE_DX12 JPH_USE_VK JPH_USE_MTL JPH_USE_CPU_COMPUTE)
  set(${opt} OFF CACHE BOOL "" FORCE)
endforeach()
FetchContent_Declare(JoltPhysics
  GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
  GIT_TAG e77f175595e64cb44218cc9d9d56fc365ad0e36a
  SOURCE_SUBDIR Build
  SYSTEM)
FetchContent_MakeAvailable(JoltPhysics)
target_compile_options(Jolt PRIVATE -w)  # third-party code; our -Wconversion is for our sources

add_library(obf STATIC engine/rfa.cpp engine/vfs.cpp engine/dds.cpp engine/terrain.cpp engine/con.cpp engine/sm.cpp engine/scene.cpp engine/tm.cpp engine/anim.cpp engine/physics.cpp)
target_include_directories(obf PUBLIC engine)
target_link_libraries(obf PRIVATE Jolt)

add_executable(obf_mapview engine/mapview.cpp)
target_link_libraries(obf_mapview PRIVATE obf PkgConfig::SDL2 PkgConfig::EPOXY)

enable_testing()
add_executable(test_engine engine/test_engine.cpp)
target_link_libraries(test_engine PRIVATE obf)
target_compile_options(test_engine PRIVATE -UNDEBUG)  # asserts must run in every build type
add_test(NAME engine COMMAND test_engine)
