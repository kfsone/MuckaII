# Helpers that give every MuckaII target the same flags: warnings are errors on MuckaII's own
# targets only (docs/build.md), and exceptions are off (docs/style.md).

function(muckaiiTargetDefaults target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /EHs-c-)
        target_compile_definitions(${target} PRIVATE _HAS_EXCEPTIONS=0)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -fno-exceptions)
    endif()
endfunction()

# muckaiiLayerTarget(<layer> <outVar>): the library target name of a layer, "mud" -> "muckaiiMud".
function(muckaiiLayerTarget layer out)
    string(SUBSTRING "${layer}" 0 1 head)
    string(SUBSTRING "${layer}" 1 -1 tail)
    string(TOUPPER "${head}" head)
    set(${out} "muckaii${head}${tail}" PARENT_SCOPE)
endfunction()

# MSVC's ASan runtime DLLs sit beside cl.exe and are on PATH only inside a developer prompt. An
# executable built with /fsanitize=address is given its own copy so it runs from any shell.
function(muckaiiExecutableRuntime target)
    if(MSVC AND CMAKE_CXX_FLAGS MATCHES "fsanitize=address")
        get_filename_component(compilerDir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        file(GLOB asanDlls "${compilerDir}/clang_rt.asan*_dynamic-*.dll")
        foreach(dll IN LISTS asanDlls)
            add_custom_command(
                TARGET ${target} POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${dll}" "$<TARGET_FILE_DIR:${target}>")
        endforeach()
    endif()
endfunction()

# muckaiiLibrary(<layer> SOURCES <files...> [DEPENDS <layers...>])
# Creates static library muckaii<Layer>, headers under src/<layer>/include/muckaii/<layer>/.
# Its output file is named after the target: muckaiiMud.lib, libmuckaiiMud.a.
function(muckaiiLibrary layer)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPENDS" ${ARGN})
    muckaiiLayerTarget(${layer} target)
    add_library(${target} STATIC ${ARG_SOURCES})
    target_include_directories(${target} PUBLIC include)
    foreach(dep IN LISTS ARG_DEPENDS)
        muckaiiLayerTarget(${dep} depTarget)
        target_link_libraries(${target} PUBLIC ${depTarget})
    endforeach()
    muckaiiTargetDefaults(${target})
endfunction()

# muckaiiTests(<layer> SOURCES <files...>)
# Creates muckaii<Layer>Tests (docs/build.md "Targets") and registers each case with CTest.
function(muckaiiTests layer)
    cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})
    muckaiiLayerTarget(${layer} libTarget)
    set(target ${libTarget}Tests)
    add_executable(${target} ${ARG_SOURCES})
    target_link_libraries(${target} PRIVATE ${libTarget} GTest::gtest_main)
    muckaiiTargetDefaults(${target})
    muckaiiExecutableRuntime(${target})
    gtest_discover_tests(${target} DISCOVERY_MODE PRE_TEST)
endfunction()
