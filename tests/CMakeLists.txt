add_executable(kvasir_tests test_main.cpp test_utf8_string.cpp test_utf8_rope.cpp test_utf8_complex.cpp test_cow_slice.cpp test_stream.cpp)
target_link_libraries(kvasir_tests PRIVATE kvasir GTest::gtest GTest::gtest_main)
add_test(NAME kvasir_tests COMMAND kvasir_tests)
