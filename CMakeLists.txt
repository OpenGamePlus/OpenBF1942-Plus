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

add_library(obf STATIC engine/rfa.cpp engine/vfs.cpp engine/dds.cpp engine/terrain.cpp engine/con.cpp engine/sm.cpp engine/scene.cpp engine/tm.cpp)
target_include_directories(obf PUBLIC engine)

add_executable(obf_mapview engine/mapview.cpp)
target_link_libraries(obf_mapview PRIVATE obf PkgConfig::SDL2 PkgConfig::EPOXY)

enable_testing()
add_executable(test_engine engine/test_engine.cpp)
target_link_libraries(test_engine PRIVATE obf)
target_compile_options(test_engine PRIVATE -UNDEBUG)  # asserts must run in every build type
add_test(NAME engine COMMAND test_engine)
