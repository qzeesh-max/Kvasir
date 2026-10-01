#include <gtest/gtest.h>
#include <kvasir/utf8_rope.hpp>

TEST(Utf8RopeTest, DefaultConstructor) {
    kvasir::utf8_rope rope;
    EXPECT_EQ(rope.length(), 0);
    EXPECT_EQ(rope.byte_size(), 0);
    EXPECT_EQ(rope.to_string(), "");
}

TEST(Utf8RopeTest, StringConstructor) {
    kvasir::utf8_rope rope("Hello 🌍!");
    EXPECT_EQ(rope.length(), 8);
    EXPECT_EQ(rope.byte_size(), 11);
    EXPECT_EQ(rope.to_string(), "Hello 🌍!");
    EXPECT_EQ(rope[6], 0x1F30D);
}

TEST(Utf8RopeTest, ConcatAndSplit) {
    kvasir::utf8_rope r1("Hello ");
    kvasir::utf8_rope r2("🌍!");
    kvasir::utf8_rope combined = r1 + r2;
    
    EXPECT_EQ(combined.length(), 8);
    EXPECT_EQ(combined.to_string(), "Hello 🌍!");
    EXPECT_EQ(combined[6], 0x1F30D);
    
    auto [left, right] = combined.split(6);
    EXPECT_EQ(left.to_string(), "Hello ");
    EXPECT_EQ(right.to_string(), "🌍!");
}

TEST(Utf8RopeTest, InsertAndErase) {
    kvasir::utf8_rope r("Hello !");
    kvasir::utf8_rope r_inserted = r.insert(6, kvasir::utf8_rope("🌍"));
    EXPECT_EQ(r_inserted.to_string(), "Hello 🌍!");
    
    kvasir::utf8_rope r_erased = r_inserted.erase(6, 1);
    EXPECT_EQ(r_erased.to_string(), "Hello !");
}
