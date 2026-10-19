# A Passage kernel in C++, built on the QLever fork that carries the resumable
# scan. `EXCLUDE_FROM_ALL` so that adding QLever defines its targets without
# building its tests and benchmarks; only what `passage` links gets built.
cmake_minimum_required(VERSION 3.27)
project(passage-cpp CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/qlever/CMakeLists.txt)
    message(FATAL_ERROR
            "the qlever submodule is empty; run: git submodule update --init")
endif ()

add_subdirectory(qlever EXCLUDE_FROM_ALL)

add_executable(passage src/main.cpp src/Continuation.cpp)
target_include_directories(passage PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/qlever/src)
qlever_target_link_libraries(passage qlever sparqlParser engine qlever_util index)
