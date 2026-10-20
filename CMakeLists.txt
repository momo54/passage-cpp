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

# `find_package` creates IMPORTED targets that are visible only in the
# directory that called it and below. QLever calls these inside its own
# CMakeLists, so linking its libraries from here fails at generate time with
# `Target "passage" links to OpenSSL::SSL but the target was not found`. The
# same calls, made here, put the targets in this scope; they are the ones
# `qlever_target_link_libraries` adds to everything it touches.
find_package(OpenSSL REQUIRED)
find_package(ICU 60 REQUIRED COMPONENTS uc i18n)
find_package(Boost 1.81 REQUIRED CONFIG
        COMPONENTS iostreams program_options url container)

add_subdirectory(qlever EXCLUDE_FROM_ALL)

# QLever makes its dependencies' headers visible with
# `include_directories(SYSTEM ...)` at its own directory scope. That reaches
# its subdirectories but not this one, which is its parent, so compiling any
# file that includes a QLever header fails on the first such dependency:
# `'range/v3/all.hpp' file not found`, then `'fsst.h'`, and so on. Rather than
# replicate each call and go stale, read the directory property and apply the
# whole list.
get_directory_property(QLEVER_INCLUDE_DIRS
        DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/qlever INCLUDE_DIRECTORIES)
# And the same for library search paths, for the same reason: QLever calls
# `link_libraries(jemalloc)` with a plain name, resolved through a
# `link_directories` that is also scoped to its directory. Without this the
# link ends on `ld: library 'jemalloc' not found`.
get_directory_property(QLEVER_LINK_DIRS
        DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/qlever LINK_DIRECTORIES)

add_executable(passage src/main.cpp src/Continuation.cpp src/ScanIterator.cpp src/JoinIterator.cpp src/UnionIterator.cpp src/Planner.cpp src/Algebra.cpp)
target_include_directories(passage PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/qlever/src)
qlever_target_link_libraries(passage qlever sparqlParser engine qlever_util index)
target_include_directories(passage SYSTEM PRIVATE ${QLEVER_INCLUDE_DIRS})
target_link_directories(passage PRIVATE ${QLEVER_LINK_DIRS})

# The tests build their own index from Turtle, via QLever's `makeTestIndex`,
# so they need its `testUtil` target — which the comment in
# `qlever/test/CMakeLists.txt` describes as basically all of QLever.
enable_testing()
add_executable(ContinuationTest test/ContinuationTest.cpp src/Continuation.cpp src/ScanIterator.cpp src/JoinIterator.cpp src/UnionIterator.cpp src/Planner.cpp src/Algebra.cpp)
target_include_directories(ContinuationTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/qlever/src
        ${CMAKE_CURRENT_SOURCE_DIR}/qlever/test)
qlever_target_link_libraries(ContinuationTest gtest gmock_main testUtil
        qlever sparqlParser engine qlever_util index)
target_include_directories(ContinuationTest SYSTEM PRIVATE ${QLEVER_INCLUDE_DIRS})
target_link_directories(ContinuationTest PRIVATE ${QLEVER_LINK_DIRS})
add_test(NAME ContinuationTest COMMAND ContinuationTest)

add_executable(AlgebraTest test/AlgebraTest.cpp src/Algebra.cpp)
target_include_directories(AlgebraTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/qlever/src
        ${CMAKE_CURRENT_SOURCE_DIR}/qlever/test)
target_include_directories(AlgebraTest SYSTEM PRIVATE ${QLEVER_INCLUDE_DIRS})
target_link_directories(AlgebraTest PRIVATE ${QLEVER_LINK_DIRS})
qlever_target_link_libraries(AlgebraTest gtest gmock_main testUtil
        qlever sparqlParser engine qlever_util index)
add_test(NAME AlgebraTest COMMAND AlgebraTest)

add_executable(PassageCasesTest test/PassageCasesTest.cpp src/Continuation.cpp
        src/ScanIterator.cpp src/JoinIterator.cpp src/UnionIterator.cpp
        src/Planner.cpp src/Algebra.cpp)
target_include_directories(PassageCasesTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/qlever/src
        ${CMAKE_CURRENT_SOURCE_DIR}/qlever/test)
target_include_directories(PassageCasesTest SYSTEM PRIVATE ${QLEVER_INCLUDE_DIRS})
target_link_directories(PassageCasesTest PRIVATE ${QLEVER_LINK_DIRS})
qlever_target_link_libraries(PassageCasesTest gtest gmock_main testUtil
        qlever sparqlParser engine qlever_util index)
add_test(NAME PassageCasesTest COMMAND PassageCasesTest)
