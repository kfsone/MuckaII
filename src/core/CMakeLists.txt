muckaiiLibrary(core SOURCES core.cpp DEPENDS transport mud persistence session client)

# Test support (docs/build.md "Targets"): the loopback fake MUD2 server.
add_library(muckaiiCoreFakeServer STATIC fakeServer/fakeMudServer.cpp)
target_include_directories(muckaiiCoreFakeServer PUBLIC fakeServer/include)
target_link_libraries(muckaiiCoreFakeServer PUBLIC muckaiiTransportLoopback muckaiiMudGolden)
muckaiiTargetDefaults(muckaiiCoreFakeServer)

muckaiiTests(core SOURCES coreTests.cpp)
target_link_libraries(muckaiiCoreTests PRIVATE muckaiiCoreFakeServer sqliteAmalgamation)
target_compile_definitions(muckaiiCoreTests PRIVATE MUCKAII_REPO_ROOT="${PROJECT_SOURCE_DIR}")
