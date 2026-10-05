cmake_minimum_required(VERSION 3.24)
project(sm64nv C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SM64_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libsm64)
set(SM64_GEN ${SM64_DIR}/src/decomp/mario/geo.inc.c ${SM64_DIR}/src/decomp/mario/model.inc.c)
add_custom_command(OUTPUT ${SM64_GEN}
    COMMAND python3 import-mario-geo.py
    WORKING_DIRECTORY ${SM64_DIR})

set(SM64_SRC_DIRS src src/decomp src/decomp/engine src/decomp/include/PR src/decomp/game src/decomp/pc
    src/decomp/pc/audio src/decomp/tools src/decomp/audio)
set(SM64_SRC ${SM64_GEN})
foreach(dir ${SM64_SRC_DIRS})
    file(GLOB found ${SM64_DIR}/${dir}/*.c)
    list(APPEND SM64_SRC ${found})
endforeach()

add_library(sm64 STATIC ${SM64_SRC})
target_include_directories(sm64 PRIVATE ${SM64_DIR}/src/decomp/include)
target_include_directories(sm64 PUBLIC ${SM64_DIR}/src)
target_compile_definitions(sm64 PUBLIC SM64_LIB_EXPORT PRIVATE GBI_FLOATS VERSION_US NO_SEGMENTED_MEMORY)
target_compile_options(sm64 PRIVATE -fno-strict-aliasing -w)

add_library(sm64nv_core STATIC src/core/sha256.cpp src/core/rom.cpp src/core/frame.cpp src/core/surfaces.cpp)
target_include_directories(sm64nv_core PUBLIC src)
target_link_libraries(sm64nv_core PUBLIC sm64)
target_compile_options(sm64nv_core PRIVATE -Wall -Wextra)

if(NOT WIN32)
    enable_testing()
    file(GLOB TEST_SRC tests/*.cpp)
    add_executable(core_tests ${TEST_SRC})
    target_include_directories(core_tests PRIVATE tests)
    target_link_libraries(core_tests PRIVATE sm64nv_core m)
    add_test(NAME core_tests COMMAND core_tests)
endif()
