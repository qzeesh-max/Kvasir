#include <gtest/gtest.h>
#include <sstream>
#include "kvasir/utf8_string.hpp"
#include "kvasir/utf8_string_view.hpp"
#include "kvasir/utf8_rope.hpp"
#include "kvasir/utf8_slice.hpp"
#include "kvasir/utf8_cow.hpp"

using namespace kvasir;

TEST(StreamTest, Utf8StringStream) {
    utf8_string s("Hello World");
    std::ostringstream oss;
    oss << s;
    EXPECT_EQ(oss.str(), "Hello World");

    std::istringstream iss("Testing");
    utf8_string s2;
    iss >> s2;
    EXPECT_EQ(s2, utf8_string("Testing"));
}

TEST(StreamTest, Utf8StringViewStream) {
    utf8_string s("Hello World");
    utf8_string_view v(s, 0, 5); // "Hello"
    std::ostringstream oss;
    oss << v;
    EXPECT_EQ(oss.str(), "Hello");
}

TEST(StreamTest, Utf8RopeStream) {
    utf8_rope rope(utf8_string("Hello "));
    rope = rope + utf8_rope(utf8_string("World"));
    
    std::ostringstream oss;
    oss << rope;
    EXPECT_EQ(oss.str(), "Hello World");

    std::istringstream iss("TestingRope");
    utf8_rope rope2;
    iss >> rope2;
    EXPECT_EQ(rope2.to_string(), utf8_string("TestingRope"));
}

TEST(StreamTest, Utf8SliceStream) {
    utf8_string s("Hello World");
    utf8_slice slice(s, 6, 5); // "World"
    
    std::ostringstream oss;
    oss << slice;
    EXPECT_EQ(oss.str(), "World");
}

TEST(StreamTest, Utf8CowStream) {
    utf8_cow cow(utf8_string("Hello World"));
    
    std::ostringstream oss;
    oss << cow;
    EXPECT_EQ(oss.str(), "Hello World");

    std::istringstream iss("TestingCow");
    utf8_cow cow2;
    iss >> cow2;
    EXPECT_EQ(cow2.slice().to_string(), utf8_string("TestingCow"));
}
