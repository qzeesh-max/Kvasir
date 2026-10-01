#include <gtest/gtest.h>
#include <kvasir/utf8_string.hpp>
#include <kvasir/utf8_rope.hpp>
#include <string>
#include <unordered_map>
#include <map>

using namespace kvasir;

TEST(Utf8ComplexTest, MapIntegration) {
    std::map<utf8_string, int> dict;
    dict[utf8_string("Hello")] = 1;
    dict[utf8_string("World")] = 2;
    dict[utf8_string("こんにちは")] = 3; // Konnichiwa
    
    EXPECT_EQ(dict[utf8_string("Hello")], 1);
    EXPECT_EQ(dict[utf8_string("こんにちは")], 3);
}

TEST(Utf8ComplexTest, UnorderedMapIntegration) {
    std::unordered_map<utf8_string, int> hash_map;
    hash_map[utf8_string("Hello")] = 1;
    hash_map[utf8_string("World")] = 2;
    hash_map[utf8_string("こんにちは")] = 3;
    
    EXPECT_EQ(hash_map[utf8_string("Hello")], 1);
    EXPECT_EQ(hash_map[utf8_string("こんにちは")], 3);
}

TEST(Utf8ComplexTest, RopeMapIntegration) {
    std::map<utf8_rope, int> dict;
    dict[utf8_rope("Hello")] = 1;
    dict[utf8_rope("World")] = 2;
    dict[utf8_rope("こんにちは")] = 3;
    
    EXPECT_EQ(dict[utf8_rope("Hello")], 1);
    EXPECT_EQ(dict[utf8_rope("こんにちは")], 3);
}

TEST(Utf8ComplexTest, RopeUnorderedMapIntegration) {
    std::unordered_map<utf8_rope, int> hash_map;
    hash_map[utf8_rope("Hello")] = 1;
    hash_map[utf8_rope("World")] = 2;
    hash_map[utf8_rope("こんにちは")] = 3;
    
    EXPECT_EQ(hash_map[utf8_rope("Hello")], 1);
    EXPECT_EQ(hash_map[utf8_rope("こんにちは")], 3);
}

TEST(Utf8ComplexTest, NeedleInHaystackString) {
    utf8_string haystack("This is a long text with emojis 😊, and some Chinese characters 漢字.");
    
    EXPECT_EQ(haystack.find(utf8_string("emojis")), 25);
    EXPECT_EQ(haystack.find(utf8_string("😊")), 32);
    EXPECT_EQ(haystack.find(utf8_string("Chinese")), 44);
    EXPECT_EQ(haystack.find(utf8_string("漢字")), 63);
    EXPECT_EQ(haystack.find(utf8_string("missing")), std::string::npos);
}

TEST(Utf8ComplexTest, NeedleInHaystackRope) {
    utf8_rope haystack("This is a long text with emojis 😊, and some Chinese characters 漢字.");
    
    EXPECT_EQ(haystack.find(utf8_string("emojis")), 25);
    EXPECT_EQ(haystack.find(utf8_string("😊")), 32);
    EXPECT_EQ(haystack.find(utf8_string("Chinese")), 44);
    EXPECT_EQ(haystack.find(utf8_string("漢字")), 63);
    EXPECT_EQ(haystack.find(utf8_string("missing")), std::string::npos);
}

// Emulate upper/lower case conversion using iterations
TEST(Utf8ComplexTest, AsciiToUpperCase) {
    utf8_string lower("this is a test 😊 漢字");
    std::string upper_bytes;
    for (auto cp : lower) {
        if (cp >= 'a' && cp <= 'z') {
            upper_bytes.push_back(static_cast<char>(cp - 32));
        } else if (cp <= 0x7F) {
            upper_bytes.push_back(static_cast<char>(cp));
        } else {
            // Re-encode non-ASCII (simple hack just for testing)
            char buf[5] = {0};
            if (cp <= 0x7FF) {
                buf[0] = 0xC0 | ((cp >> 6) & 0x1F);
                buf[1] = 0x80 | (cp & 0x3F);
            } else if (cp <= 0xFFFF) {
                buf[0] = 0xE0 | ((cp >> 12) & 0x0F);
                buf[1] = 0x80 | ((cp >> 6) & 0x3F);
                buf[2] = 0x80 | (cp & 0x3F);
            } else {
                buf[0] = 0xF0 | ((cp >> 18) & 0x07);
                buf[1] = 0x80 | ((cp >> 12) & 0x3F);
                buf[2] = 0x80 | ((cp >> 6) & 0x3F);
                buf[3] = 0x80 | (cp & 0x3F);
            }
            upper_bytes.append(buf);
        }
    }
    
    utf8_string upper(upper_bytes);
    EXPECT_TRUE(upper == utf8_string("THIS IS A TEST 😊 漢字"));
}
