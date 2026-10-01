#include <gtest/gtest.h>
#include <kvasir/utf8_string.hpp>

TEST(Utf8StringTest, DefaultConstructor) {
    kvasir::utf8_string s;
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0);
    EXPECT_EQ(s.length(), 0);
}

TEST(Utf8StringTest, ConstructorFromCString) {
    kvasir::utf8_string s("Hello");
    EXPECT_EQ(s.length(), 5);
    EXPECT_EQ(s[0], 'H');
    EXPECT_EQ(s[4], 'o');
}

TEST(Utf8StringTest, ConstructorFromUtf8String) {
    // 🌍 is 4 bytes (U+1F30D)
    // é is 2 bytes (U+00E9)
    kvasir::utf8_string s("Hello 🌍! Még");
    // "Hello " = 6 chars
    // 🌍 = 1 char
    // "! M" = 3 chars
    // é = 1 char
    // "g" = 1 char
    // Total = 6 + 1 + 3 + 1 + 1 = 12 code points
    EXPECT_EQ(s.length(), 12);
    EXPECT_EQ(s[0], 'H');
    EXPECT_EQ(s[6], 0x1F30D);
    EXPECT_EQ(s[7], '!');
    EXPECT_EQ(s[10], 0x00E9);
    EXPECT_EQ(s[11], 'g');
}

TEST(Utf8StringTest, LargeStringIndex) {
    std::string large_str;
    for (int i = 0; i < 200; ++i) {
        large_str += "a🌍";
    }
    kvasir::utf8_string s(large_str);
    EXPECT_EQ(s.length(), 400);
    EXPECT_EQ(s[0], 'a');
    EXPECT_EQ(s[1], 0x1F30D);
    EXPECT_EQ(s[398], 'a');
    EXPECT_EQ(s[399], 0x1F30D);
}

TEST(Utf8StringTest, Iterators) {
    kvasir::utf8_string s("a🌍b");
    auto it = s.begin();
    EXPECT_EQ(*it, 'a');
    ++it;
    EXPECT_EQ(*it, 0x1F30D);
    it++;
    EXPECT_EQ(*it, 'b');
    ++it;
    EXPECT_EQ(it, s.end());
    
    // random access
    EXPECT_EQ(s.end() - s.begin(), 3);
    EXPECT_EQ(*(s.begin() + 1), 0x1F30D);
}

TEST(Utf8StringTest, Mutators) {
    kvasir::utf8_string s("a");
    s.push_back(0x1F30D); // 🌍
    EXPECT_EQ(s.length(), 2);
    EXPECT_EQ(s[1], 0x1F30D);
    
    s += "b";
    EXPECT_EQ(s.length(), 3);
    EXPECT_EQ(s[2], 'b');
    
    s.append("cd🌍");
    EXPECT_EQ(s.length(), 6);
    EXPECT_EQ(s[5], 0x1F30D);
}

TEST(Utf8StringTest, Comparisons) {
    kvasir::utf8_string s1("hello");
    kvasir::utf8_string s2("hello");
    kvasir::utf8_string s3("world");
    std::string s4("hello");
    
    EXPECT_TRUE(s1 == s2);
    EXPECT_FALSE(s1 == s3);
    EXPECT_TRUE(s1 == s4);
    EXPECT_TRUE(s4 == s1);
    EXPECT_TRUE(s1 != s3);
}

#include <kvasir/utf8_string_view.hpp>

TEST(Utf8StringViewTest, BasicView) {
    kvasir::utf8_string s("Hello 🌍");
    kvasir::utf8_string_view v(s);
    
    EXPECT_EQ(v.length(), 7);
    EXPECT_EQ(v[6], 0x1F30D);
    
    kvasir::utf8_string_view sub = v.substr(6, 1);
    EXPECT_EQ(sub.length(), 1);
    EXPECT_EQ(sub[0], 0x1F30D);
    
    kvasir::utf8_string s2 = sub.to_string();
    EXPECT_EQ(s2.length(), 1);
    EXPECT_EQ(s2[0], 0x1F30D);
}

TEST(Utf8StringTest, StdStringParityMethods) {
    kvasir::utf8_string str("Hello 🌍! Bonjour!");
    
    // Substring
    kvasir::utf8_string sub = str.substr(6, 2); // "🌍!"
    EXPECT_EQ(sub.size(), 2);
    EXPECT_EQ(sub[0], 0x1F30D);
    EXPECT_EQ(sub[1], '!');
    
    // Find
    size_t pos = str.find(kvasir::utf8_string("🌍"));
    EXPECT_EQ(pos, 6);
    pos = str.find(kvasir::utf8_string("Bonjour"));
    EXPECT_EQ(pos, 9);
    pos = str.find(kvasir::utf8_string("NotThere"));
    EXPECT_EQ(pos, std::string::npos);
    
    // Insert
    str.insert(6, kvasir::utf8_string("Beautiful "));
    EXPECT_EQ(str.internal_data().find("Beautiful 🌍!"), 6);
    
    // Replace
    str.replace(6, 9, kvasir::utf8_string("Huge")); // Replaces "Beautiful" with "Huge"
    EXPECT_EQ(str.internal_data().find("Huge 🌍!"), 6);
    
    // Erase
    str.erase(6, 5); // Erases "Huge "
    EXPECT_EQ(str.internal_data().find("Hello 🌍!"), 0);
}

TEST(Utf8StringTest, ValidateUtf8) {
    EXPECT_TRUE(kvasir::utf8_string::validate_utf8("Hello World!"));
    EXPECT_TRUE(kvasir::utf8_string::validate_utf8("🌍 is beautiful!"));
    
    // Invalid UTF-8 cases
    EXPECT_FALSE(kvasir::utf8_string::validate_utf8("\x80")); // Unexpected continuation byte
    EXPECT_FALSE(kvasir::utf8_string::validate_utf8("\xC0\xAF")); // Overlong encoding
    EXPECT_FALSE(kvasir::utf8_string::validate_utf8("\xE0\x9F\xBF")); // Overlong encoding
    EXPECT_FALSE(kvasir::utf8_string::validate_utf8("\xED\xA0\x80")); // Surrogates
    EXPECT_FALSE(kvasir::utf8_string::validate_utf8("\xF4\x90\x80\x80")); // Out of bounds
}

TEST(Utf8StringTest, BatchDecode) {
    kvasir::utf8_string str("Hello 🌍! Bonjour!");
    uint32_t buf[20];
    
    size_t decoded = str.decode_code_points(0, 10, buf);
    EXPECT_EQ(decoded, 10);
    EXPECT_EQ(buf[0], 'H');
    EXPECT_EQ(buf[5], ' ');
    EXPECT_EQ(buf[6], 0x1F30D);
    EXPECT_EQ(buf[7], '!');
}

TEST(Utf8StringTest, StdHashTransparent) {
    std::hash<kvasir::utf8_string> hasher;
    kvasir::utf8_string str("Hello 🌍");
    std::string_view sv = str.internal_data();
    
    EXPECT_EQ(hasher(str), hasher(sv));
}
