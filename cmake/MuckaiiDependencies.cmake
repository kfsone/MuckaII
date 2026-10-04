# Third-party code: FetchContent from a pinned release URL with URL_HASH (docs/build.md,
# "Dependencies"). Nothing third-party is committed. Slint and miniaudio are added when a target
# needs them.
include(FetchContent)

set(INSTALL_GTEST          OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON  CACHE BOOL "" FORCE)
set(BUILD_GMOCK            OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    googletest
    URL      https://github.com/google/googletest/archive/refs/tags/v1.18.0.tar.gz
    URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
    SYSTEM)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# SQLite (persistence): the amalgamation, built here as one static C library. Its target name is
# not muckaii<Word>, so the layer check does not take it for a layer.
FetchContent_Declare(
    sqlite3
    URL      https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip
    URL_HASH SHA3_256=628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e
    SYSTEM)
FetchContent_MakeAvailable(sqlite3)
add_library(sqliteAmalgamation STATIC "${sqlite3_SOURCE_DIR}/sqlite3.c")
target_include_directories(sqliteAmalgamation SYSTEM PUBLIC "${sqlite3_SOURCE_DIR}")
target_compile_definitions(sqliteAmalgamation PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0)

# FTXUI (tui).
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    ftxui
    URL      https://github.com/ArthurSonzogni/FTXUI/archive/refs/tags/v7.0.3.tar.gz
    URL_HASH SHA256=e7c62ffe19009759821b4f0f8df7f2a6fb83784c3a9f1477d81f56d3ee723c88
    SYSTEM)
# FTXUI's headers and sources use try/catch (through <chrono> and <ostream>), so it needs the
# exception model that MSVC's /EHsc names; the presets set CMAKE_CXX_FLAGS whole and leave it out,
# which is warning C4530. The flag is scoped to FTXUI's own directory tree (docs/build.md).
block()
    if(MSVC)
        string(APPEND CMAKE_CXX_FLAGS " /EHsc")
    endif()
    FetchContent_MakeAvailable(ftxui)
endblock()
