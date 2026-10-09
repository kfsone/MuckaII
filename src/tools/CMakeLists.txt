# Manual wire-log tools (docs/build.md "Targets"). Not a layer: nothing links to them.
add_library(muckaiiWireAudit STATIC wireAudit.cpp wireCheckOptions.cpp)
target_include_directories(muckaiiWireAudit PUBLIC .)
target_link_libraries(muckaiiWireAudit PUBLIC muckaiiMudGolden muckaiiPersistence)
muckaiiTargetDefaults(muckaiiWireAudit)

add_executable(muckaiiWireCheck wireCheck.cpp)
target_link_libraries(muckaiiWireCheck PRIVATE muckaiiWireAudit)
muckaiiTargetDefaults(muckaiiWireCheck)
muckaiiExecutableRuntime(muckaiiWireCheck)

add_executable(muckaiiWireAuditTests wireAuditTests.cpp)
target_link_libraries(muckaiiWireAuditTests PRIVATE muckaiiWireAudit GTest::gtest_main)
muckaiiTargetDefaults(muckaiiWireAuditTests)
muckaiiExecutableRuntime(muckaiiWireAuditTests)
gtest_discover_tests(muckaiiWireAuditTests DISCOVERY_MODE PRE_TEST)
