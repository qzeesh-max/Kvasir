#include <gtest/gtest.h>
#include "kvasir/utf8_string.hpp"
#include "kvasir/utf8_string_view.hpp"
#include "kvasir/utf8_rope.hpp"
#include "kvasir/utf8_slice.hpp"
#include "kvasir/utf8_cow.hpp"

using namespace kvasir;

TEST(CoverageGapTest, Utf8StringViewMethods) {
    utf8_string_view v;
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.size(), 0);
    EXPECT_EQ(v.begin(), v.end());
    
    utf8_string s("Test");
    utf8_string_view v2(s);
    utf8_string_view v3(s);
    EXPECT_TRUE(v2 == v3);
    
    utf8_string s_other("Other");
    utf8_string_view v4(s_other);
    EXPECT_FALSE(v2 == v4);
    
    utf8_string_view v5(s, 0, 3);
    EXPECT_FALSE(v2 == v5);
}

TEST(CoverageGapTest, Utf8StringIteratorsAndCompare) {
    utf8_string s1("A");
    utf8_string s2("B");
    
    auto it1 = s1.begin();
    auto it2 = s1.end();
    
    EXPECT_EQ(it2 - it1, 1);
    EXPECT_TRUE(it1 < it2);
    EXPECT_FALSE(it1 > it2);
    EXPECT_TRUE(it1 <= it2);
    EXPECT_FALSE(it1 >= it2);
    EXPECT_TRUE(it2 >= it1);
    EXPECT_TRUE(it1 <= it1);
    
    EXPECT_EQ(s1.compare(s2), -1);
    EXPECT_EQ(s1.compare(std::string("B")), -1);
    
    EXPECT_TRUE(s1 != s2);
    EXPECT_TRUE(s1 < s2);
    EXPECT_TRUE(s1 != std::string("B"));
    EXPECT_TRUE(s1 < std::string("B"));
    EXPECT_TRUE(std::string("A") != s2);
    EXPECT_TRUE(std::string("A") < s2);
    
    EXPECT_TRUE(s1 != "B");
    EXPECT_TRUE(s1 < "B");
    EXPECT_TRUE("A" != s2);
    EXPECT_TRUE("A" < s2);
    EXPECT_TRUE("A" == s1);
    EXPECT_TRUE(s1 == "A");
    EXPECT_TRUE(std::string("A") == s1);
    EXPECT_TRUE(s1 == std::string("A"));
}

TEST(CoverageGapTest, Utf8StringMutatorsAndSwap) {
    utf8_string s1("Hello");
    s1.append(std::string(" World"));
    EXPECT_EQ(s1, "Hello World");
    
    s1.append(utf8_string("!"));
    EXPECT_EQ(s1, "Hello World!");
    
    s1 += std::string(" Again");
    s1 += utf8_string(".");
    EXPECT_EQ(s1, "Hello World! Again.");
    
    utf8_string s2("SwapMe");
    s1.swap(s2);
    EXPECT_EQ(s1, "SwapMe");
    EXPECT_EQ(s2, "Hello World! Again.");
}

TEST(CoverageGapTest, Utf8StringPushBackMultiByte) {
    utf8_string s;
    s.push_back(0x00A3); // £ (2 bytes)
    s.push_back(0x20AC); // € (3 bytes)
    EXPECT_EQ(s.length(), 2);
    EXPECT_EQ(s[0], 0x00A3);
    EXPECT_EQ(s[1], 0x20AC);
}

TEST(CoverageGapTest, Utf8StringSIMDFastPaths) {
    // Generate a very long ASCII string to trigger SIMD paths
    std::string long_ascii(500, 'A');
    utf8_string s(long_ascii); // triggers creation fast path
    
    EXPECT_EQ(s.length(), 500);
    EXPECT_EQ(s[400], 'A'); // triggers indexing fast path
    
    std::vector<uint32_t> out(500);
    s.decode_code_points(0, 500, out.data()); // triggers batch_decode SIMD
    EXPECT_EQ(out[400], 'A');
}

TEST(CoverageGapTest, Utf8RopeCoverage) {
    utf8_rope r1(utf8_string("Apple"));
    utf8_rope r2(utf8_string("Banana"));
    
    EXPECT_TRUE(r1 != r2);
    EXPECT_TRUE(r1 < r2);
    
    utf8_rope empty;
    utf8_rope erased = r1.erase(0, 0); // count = 0
    EXPECT_EQ(erased, r1);
    
    utf8_rope erased2 = r1.erase(10, 5); // pos >= length
    EXPECT_EQ(erased2, r1);
}

TEST(CoverageGapTest, Utf8CowSliceCoverage) {
    utf8_cow c1;
    EXPECT_TRUE(!c1.is_owned());
    
    utf8_cow c2(utf8_rope(utf8_string("RopeCow"))); // explicit utf8_cow(utf8_rope&&)
    EXPECT_TRUE(c2.is_owned());
    
    utf8_cow c3(std::move(c2));
    EXPECT_TRUE(c3.is_owned());
    
    utf8_cow c4;
    c4 = std::move(c3);
    EXPECT_TRUE(c4.is_owned());
    
    utf8_slice s1;
    EXPECT_TRUE(s1.empty());
    
    utf8_slice s2(utf8_string("Test"));
    EXPECT_TRUE(s1 != s2);
}

TEST(CoverageGapTest, Utf8StringViewEdgeCases) {
    utf8_string s("Hello");
    utf8_string_view v1(s);
    auto it = v1.begin();
    EXPECT_EQ(*it, 'H');
    
    utf8_string_view v_empty;
    EXPECT_EQ(v_empty.begin(), v_empty.end());
    EXPECT_EQ(v_empty.size(), 0);
}

TEST(CoverageGapTest, Utf8StringExtraMethods) {
    utf8_string s(std::string_view("StringView"));
    utf8_string s2;
    s2 = s;
    EXPECT_EQ(s2, "StringView");
    
    EXPECT_STREQ(s.c_str(), "StringView");
    
    auto it = s.begin();
    it += 2;
    EXPECT_EQ(*it, 'r');
    it -= 1;
    EXPECT_EQ(*it, 't');
    --it;
    EXPECT_EQ(*it, 'S');
    it++;
    EXPECT_EQ(*it, 't');
    auto it2 = it + 5; // index 1 + 5 = 6 ('V')
    EXPECT_EQ(*it2, 'V');
    auto it3 = 2 + it; // index 1 + 2 = 3 ('i')
    EXPECT_EQ(*it3, 'i');
    auto it4 = it3 - 2; // index 3 - 2 = 1 ('t')
    EXPECT_EQ(*it4, 't');
}

TEST(CoverageGapTest, Utf8RopeExtraMethods) {
    utf8_rope r(std::string_view("RopeView"));
    r += utf8_rope(utf8_string(" Append"));
    EXPECT_EQ(r.to_string(), "RopeView Append");
    
    size_t pos = r.find(utf8_rope(utf8_string("View")));
    EXPECT_EQ(pos, 4);
}

TEST(CoverageGapTest, Utf8CowFallbacks) {
    utf8_cow cow_str(utf8_string("Str"));
    utf8_cow cow_rope(utf8_rope(utf8_string("Rope")));
    
    utf8_string s1 = cow_str.slice().to_string();
    EXPECT_EQ(s1, "Str");
    
    utf8_cow c_owned(utf8_string("OwnedStr"));
    utf8_string s2 = c_owned.slice().to_string();
    EXPECT_EQ(s2, "OwnedStr");
}

TEST(CoverageGapTest, Utf8SliceEdgeCases) {
    utf8_string s("Slice");
    utf8_slice slice1(s, 10, 5); // start > len
    EXPECT_TRUE(slice1.empty());
    
    utf8_rope r(s);
    utf8_slice slice2(r, 10, 5); // start > len
    EXPECT_TRUE(slice2.empty());
    
    utf8_slice slice3(s, 0, 5);
    utf8_slice slice4(s, 0, 4);
    EXPECT_FALSE(slice3 == slice4);
    
    utf8_string s_diff("Slicf");
    utf8_slice slice5(s_diff, 0, 5);
    EXPECT_FALSE(slice3 == slice5);
}
