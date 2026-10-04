cmake_minimum_required(VERSION 4.3)

# MSVC debug information and runtime checks are selected by abstraction, not by /Zi and /RTC1 in
# the flag strings (cmake/MuckaiiToolchain.cmake). Both must be set before project(). CMake 4.3
# sets CMP0141 and CMP0184 to NEW already; the variables are what MuckaII chooses.
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>")
set(CMAKE_MSVC_RUNTIME_CHECKS "")

project(MuckaII VERSION 0.0.0 LANGUAGES CXX C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(MuckaiiToolchain)

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

include(CTest)
include(MuckaiiTargets)
include(MuckaiiDependencies)
include(MuckaiiLayers)

# One library per layer. The order is the order of docs/spec/architecture.md.
add_subdirectory(src/transport)
add_subdirectory(src/mud)
add_subdirectory(src/persistence)
add_subdirectory(src/session)
add_subdirectory(src/client)
add_subdirectory(src/audio)
add_subdirectory(src/core)
add_subdirectory(src/tui)

# Dependency direction (architecture.md "Layers"): fails the configure on an upward or sideways link.
muckaiiCheckLayers()

# The repo gates run as tests: `ctest` therefore runs every test target and the scripts.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(
    NAME repoGates
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-all.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
add_test(
    NAME repoTidy
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-tidy.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
add_test(
    NAME repoLayerIncludes
    COMMAND "${CMAKE_COMMAND}" "-DmuckaiiRoot=${CMAKE_CURRENT_SOURCE_DIR}" -P
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/MuckaiiLayers.cmake")
