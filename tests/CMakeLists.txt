add_executable(${PROJECT_NAME}_tests test_main.cpp)

target_link_libraries(${PROJECT_NAME}_tests PRIVATE gtest gtest_main sigil_core)

add_test(NAME basic_test COMMAND ${PROJECT_NAME}_tests)
