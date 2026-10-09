# MSVC flag ownership (docs/build.md, "Presets"). CMake's MSVC defaults carry /EHsc, /RTC1 and
# /Zi, which contradict what MuckaII decides (no exceptions in our code, ASan in Debug), and cl
# prints D9025 "overriding" for every contradiction. The flags are therefore settled here, once,
# whatever the configure was given: a preset, a plain `cmake -G`, or the Visual Studio IDE.
#
# Included right after project(); this file runs in the top-level scope, so every directory below
# (ours and FetchContent's) sees the settled flags.

if(NOT MSVC)
    return()
endif()

option(MUCKAII_ASAN "Build Debug with /fsanitize=address (MSVC)" ON)

# What the inherited flags may not contain: the exception model (each target says its own), the
# runtime checks (/RTC is rejected with ASan), and edit-and-continue debug info (/ZI, also rejected
# with ASan). Debug information comes from CMAKE_MSVC_DEBUG_INFORMATION_FORMAT (set before
# project(), CMakeLists.txt).
function(muckaiiStripFlags var)
    set(value "${${var}}")
    while(value MATCHES "(^| )/(EH[a-z-]*|RTC[a-z0-9]*|ZI)( |$)")
        string(REGEX REPLACE "(^| )/(EH[a-z-]*|RTC[a-z0-9]*|ZI)( |$)" " " value "${value}")
    endwhile()
    string(STRIP "${value}" value)
    set(${var} "${value}" PARENT_SCOPE)
endfunction()

foreach(lang CXX C)
    foreach(suffix "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
        muckaiiStripFlags(CMAKE_${lang}_FLAGS${suffix})
    endforeach()
endforeach()

# The Visual Studio generator compiles one file at a time per project unless asked.
add_compile_options("$<$<COMPILE_LANGUAGE:CXX,C>:/MP>")

if(MUCKAII_ASAN)
    add_compile_options("$<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:CXX,C>>:/fsanitize=address>")
    # ASan and incremental linking do not mix.
    foreach(kind EXE SHARED MODULE)
        string(REGEX REPLACE "/INCREMENTAL(:NO)?" "" CMAKE_${kind}_LINKER_FLAGS_DEBUG
               "${CMAKE_${kind}_LINKER_FLAGS_DEBUG}")
        string(APPEND CMAKE_${kind}_LINKER_FLAGS_DEBUG " /INCREMENTAL:NO")
    endforeach()
endif()
