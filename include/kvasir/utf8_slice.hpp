#pragma once

#include "utf8_string.hpp"
#include "utf8_rope.hpp"
#include <variant>
#include <algorithm>
#include <iostream>

namespace kvasir {

class utf8_slice {
    std::variant<const utf8_string*, const utf8_rope*> source_;
    size_t start_;
    size_t len_;

public:
    utf8_slice() : source_(static_cast<const utf8_string*>(nullptr)), start_(0), len_(0) {}
    
    utf8_slice(const utf8_string& str) 
        : source_(&str), start_(0), len_(str.length()) {}
        
    utf8_slice(const utf8_rope& rope) 
        : source_(&rope), start_(0), len_(rope.length()) {}
    
    utf8_slice(const utf8_string& str, size_t start_cp, size_t len_cp = std::string::npos) 
        : source_(&str), start_(start_cp) {
        size_t slen = str.length();
        if (start_cp >= slen) {
            start_ = slen;
            len_ = 0;
        } else {
            len_ = std::min(len_cp, slen - start_cp);
        }
    }
        
    utf8_slice(const utf8_rope& rope, size_t start_cp, size_t len_cp = std::string::npos) 
        : source_(&rope), start_(start_cp) {
        size_t rlen = rope.length();
        if (start_cp >= rlen) {
            start_ = rlen;
            len_ = 0;
        } else {
            len_ = std::min(len_cp, rlen - start_cp);
        }
    }

    bool empty() const { return len_ == 0; }
    size_t size() const { return len_; }
    size_t length() const { return len_; }

    uint32_t operator[](size_t index) const {
        if (index >= len_) return 0;
        return std::visit([this, index](auto&& arg) -> uint32_t {
            if (!arg) return 0;
            return (*arg)[start_ + index];
        }, source_);
    }

    utf8_slice substr(size_t pos = 0, size_t count = std::string::npos) const {
        if (pos >= len_) return utf8_slice();
        size_t rcount = std::min(count, len_ - pos);
        utf8_slice res = *this;
        res.start_ += pos;
        res.len_ = rcount;
        return res;
    }

    utf8_string to_string() const {
        utf8_string res;
        for (size_t i = 0; i < len_; ++i) {
            res.push_back((*this)[i]);
        }
        return res;
    }
};

inline bool operator==(const utf8_slice& lhs, const utf8_slice& rhs) {
    if (lhs.length() != rhs.length()) return false;
    for (size_t i = 0; i < lhs.length(); ++i) {
        if (lhs[i] != rhs[i]) return false;
    }
    return true;
}

inline bool operator!=(const utf8_slice& lhs, const utf8_slice& rhs) { 
    return !(lhs == rhs); 
}

inline std::ostream& operator<<(std::ostream& os, const utf8_slice& slice) {
    return os << slice.to_string();
}

} // namespace kvasir
