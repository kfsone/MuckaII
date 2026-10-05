add_executable(muckaiiTui main.cpp scroll.cpp)
target_link_libraries(muckaiiTui PRIVATE muckaiiCore muckaiiClient ftxui::component ftxui::dom ftxui::screen)
target_compile_definitions(muckaiiTui PRIVATE MUCKAII_VERSION="${PROJECT_VERSION}")
muckaiiTargetDefaults(muckaiiTui)
muckaiiExecutableRuntime(muckaiiTui)

# The scroll's line model, drawn to an off-screen FTXUI screen.
add_executable(muckaiiTuiTests scroll.cpp scrollTests.cpp)
target_link_libraries(muckaiiTuiTests PRIVATE muckaiiSession ftxui::dom ftxui::screen GTest::gtest_main)
muckaiiTargetDefaults(muckaiiTuiTests)
muckaiiExecutableRuntime(muckaiiTuiTests)
gtest_discover_tests(muckaiiTuiTests DISCOVERY_MODE PRE_TEST)
