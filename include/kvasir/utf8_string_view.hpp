#pragma once

#include "utf8_string.hpp"
#include <iostream>

namespace kvasir {

class utf8_string_view {
public:
    utf8_string_view() : str_(nullptr), start_(0), len_(0) {}
    utf8_string_view(const utf8_string& str) : str_(&str), start_(0), len_(str.length()) {}
    utf8_string_view(const utf8_string& str, size_t start_cp, size_t len_cp) 
        : str_(&str), start_(start_cp), len_(len_cp) {}

    bool empty() const { return len_ == 0; }
    size_t size() const { return len_; }
    size_t length() const { return len_; }

    uint32_t operator[](size_t index) const {
        if (index >= len_ || !str_) return 0;
        return (*str_)[start_ + index];
    }
    
    using const_iterator = utf8_string::const_iterator;
    
    const_iterator begin() const { 
        if (!str_) return const_iterator();
        return const_iterator(str_, start_); 
    }
    
    const_iterator end() const { 
        if (!str_) return const_iterator();
        return const_iterator(str_, start_ + len_); 
    }
    
    utf8_string_view substr(size_t pos = 0, size_t count = std::string::npos) const {
        if (!str_) return utf8_string_view();
        if (pos >= len_) return utf8_string_view(*str_, start_ + len_, 0);
        size_t rcount = std::min(count, len_ - pos);
        return utf8_string_view(*str_, start_ + pos, rcount);
    }
    
    // Convert back to utf8_string
    utf8_string to_string() const {
        if (!str_ || len_ == 0) return utf8_string();
        utf8_string res;
        for (size_t i = 0; i < len_; ++i) {
            res.push_back((*str_)[start_ + i]);
        }
        return res;
    }

private:
    const utf8_string* str_;
    size_t start_;
    size_t len_;
};

inline bool operator==(const utf8_string_view& lhs, const utf8_string_view& rhs) {
    if (lhs.length() != rhs.length()) return false;
    for (size_t i = 0; i < lhs.length(); ++i) {
        if (lhs[i] != rhs[i]) return false;
    }
    return true;
}

inline std::ostream& operator<<(std::ostream& os, const utf8_string_view& view) {
    return os << view.to_string();
}

} // namespace kvasir
