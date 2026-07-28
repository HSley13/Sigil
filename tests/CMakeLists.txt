include(GoogleTest)
find_package(Threads REQUIRED)

add_executable(signal_bus_tests test_signal_bus.cpp)
target_link_libraries(signal_bus_tests PRIVATE sigil_core Threads::Threads GTest::gtest_main)
gtest_discover_tests(signal_bus_tests PROPERTIES LABELS "signal_bus")

add_executable(action_base_tests test_action_base.cpp)
target_link_libraries(action_base_tests PRIVATE sigil_core GTest::gtest_main)
gtest_discover_tests(action_base_tests PROPERTIES LABELS "action_base")
