#include <gtest/gtest.h>
#include "kvasir/utf8_cow.hpp"
#include "kvasir/utf8_slice.hpp"

using namespace kvasir;

TEST(Utf8SliceTest, StringSlice) {
    utf8_string str("Hello 🌍");
    utf8_slice slice(str);
    EXPECT_EQ(slice.length(), 7); // 5 letters + space + 1 emoji
    
    auto sub = slice.substr(0, 5);
    EXPECT_EQ(sub.length(), 5);
    EXPECT_EQ(sub.to_string(), utf8_string("Hello"));

    auto sub2 = slice.substr(6, 1);
    EXPECT_EQ(sub2.length(), 1);
    EXPECT_EQ(sub2.to_string(), utf8_string("🌍"));
}

TEST(Utf8SliceTest, RopeSlice) {
    utf8_rope rope(utf8_string("Hello "));
    rope = rope + utf8_rope(utf8_string("🌍"));
    utf8_slice slice(rope);
    EXPECT_EQ(slice.length(), 7);
    
    auto sub = slice.substr(6, 1);
    EXPECT_EQ(sub.length(), 1);
    EXPECT_EQ(sub.to_string(), utf8_string("🌍"));
}

TEST(Utf8CowTest, BorrowedString) {
    utf8_string str("Hello");
    utf8_cow cow(str);
    EXPECT_TRUE(cow.is_borrowed());
    EXPECT_EQ(cow.length(), 5);
    EXPECT_EQ(cow[0], 'H');
    
    // Mutate and ensure it becomes owned
    cow.push_back('!');
    EXPECT_TRUE(cow.is_owned());
    EXPECT_EQ(cow.length(), 6);
    EXPECT_EQ(cow.slice().to_string(), utf8_string("Hello!"));
}

TEST(Utf8CowTest, BorrowedRope) {
    utf8_rope rope(utf8_string("World"));
    utf8_cow cow(rope);
    EXPECT_TRUE(cow.is_borrowed());
    EXPECT_EQ(cow.length(), 5);
    
    // Mutate and ensure it becomes owned rope
    cow.push_back('!');
    EXPECT_TRUE(cow.is_owned());
    EXPECT_EQ(cow.length(), 6);
    EXPECT_EQ(cow.slice().to_string(), utf8_string("World!"));
}

TEST(Utf8CowTest, GetMutString) {
    utf8_string str("Test");
    utf8_cow cow(str);
    auto& mstr = cow.get_mut_string();
    mstr.push_back('1');
    EXPECT_TRUE(cow.is_owned());
    EXPECT_EQ(cow.length(), 5);
}

TEST(Utf8CowTest, GetMutRope) {
    utf8_rope rope(utf8_string("Test"));
    utf8_cow cow(rope);
    auto& mrope = cow.get_mut_rope();
    mrope = mrope + utf8_rope(utf8_string("2"));
    EXPECT_TRUE(cow.is_owned());
    EXPECT_EQ(cow.length(), 5);
}
