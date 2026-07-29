include(GoogleTest)
find_package(Threads REQUIRED)

add_executable(signal_bus_tests signal_bus_tests.cpp)
target_link_libraries(signal_bus_tests PRIVATE sigil_core Threads::Threads
                                               GTest::gtest_main)
gtest_discover_tests(signal_bus_tests PROPERTIES LABELS "signal_bus")

add_executable(action_base_tests action_base_tests.cpp)
target_link_libraries(action_base_tests PRIVATE sigil_core GTest::gtest_main)
gtest_discover_tests(action_base_tests PROPERTIES LABELS "action_base")

add_executable(rule_engine_tests rule_engine_tests.cpp)
target_link_libraries(rule_engine_tests PRIVATE sigil_core GTest::gtest_main)
gtest_discover_tests(rule_engine_tests PROPERTIES LABELS "rule_engine")

add_executable(mock_sources_tests mock_sources_tests.cpp)
target_link_libraries(mock_sources_tests PRIVATE sigil_core Threads::Threads
                                                 GTest::gtest_main)
gtest_discover_tests(mock_sources_tests PROPERTIES LABELS "mock_sources")

add_executable(console_action_tests console_action_tests.cpp)
target_link_libraries(console_action_tests PRIVATE sigil_core GTest::gtest_main)
gtest_discover_tests(console_action_tests PROPERTIES LABELS "console_action")
