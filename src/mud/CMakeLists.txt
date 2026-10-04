muckaiiLibrary(mud SOURCES codes.cpp codeTable.cpp decoder.cpp)

# Test support (docs/build.md "Targets"): the capture reader, the capture replay and the golden
# serialiser.
add_library(muckaiiMudGolden STATIC golden/golden.cpp)
target_include_directories(muckaiiMudGolden PUBLIC golden/include)
target_link_libraries(muckaiiMudGolden PUBLIC muckaiiMud)
muckaiiTargetDefaults(muckaiiMudGolden)

muckaiiTests(mud SOURCES mudTests.cpp goldenTests.cpp)
target_link_libraries(muckaiiMudTests PRIVATE muckaiiMudGolden)
target_compile_definitions(muckaiiMudTests PRIVATE MUCKAII_REPO_ROOT="${PROJECT_SOURCE_DIR}")
