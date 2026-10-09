set(MUCKAII_TUI_LOGIC editKeys.cpp inputView.cpp numbers.cpp options.cpp profileEdit.cpp profileForm.cpp profileList.cpp scroll.cpp uiState.cpp)
add_executable(muckaiiTui main.cpp connect.cpp directConnect.cpp environment.cpp profileApp.cpp ${MUCKAII_TUI_LOGIC})
target_link_libraries(muckaiiTui PRIVATE muckaiiCore muckaiiClient ftxui::component ftxui::dom ftxui::screen)
target_compile_definitions(muckaiiTui PRIVATE MUCKAII_VERSION="${PROJECT_VERSION}")
muckaiiTargetDefaults(muckaiiTui)
muckaiiExecutableRuntime(muckaiiTui)

# The command line, the profile form's pure logic, and the scroll's line model drawn to an
# off-screen FTXUI screen.
add_executable(muckaiiTuiTests ${MUCKAII_TUI_LOGIC} editKeysTests.cpp inputViewTests.cpp numbersTests.cpp optionsTests.cpp profileEditTests.cpp profileFormTests.cpp profileListTests.cpp scrollTests.cpp uiStateTests.cpp)
target_link_libraries(muckaiiTuiTests PRIVATE muckaiiClient muckaiiCore muckaiiSession muckaiiPersistence ftxui::component ftxui::dom ftxui::screen
                                             GTest::gtest_main)
muckaiiTargetDefaults(muckaiiTuiTests)
muckaiiExecutableRuntime(muckaiiTuiTests)
gtest_discover_tests(muckaiiTuiTests DISCOVERY_MODE PRE_TEST)
