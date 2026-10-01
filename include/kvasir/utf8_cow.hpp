#pragma once

#include "utf8_string.hpp"
#include "utf8_rope.hpp"
#include "utf8_slice.hpp"
#include <variant>
#include <iostream>

namespace kvasir {

class utf8_cow {
    // 0: borrowed string, 1: borrowed rope, 2: owned string, 3: owned rope
    std::variant<const utf8_string*, const utf8_rope*, utf8_string, utf8_rope> data_;

public:
    utf8_cow() : data_(static_cast<const utf8_string*>(nullptr)) {}
    
    // Borrowed
    explicit utf8_cow(const utf8_string& str) : data_(&str) {}
    explicit utf8_cow(const utf8_rope& rope) : data_(&rope) {}
    
    // Owned
    explicit utf8_cow(utf8_string&& str) : data_(std::move(str)) {}
    explicit utf8_cow(utf8_rope&& rope) : data_(std::move(rope)) {}
    explicit utf8_cow(const utf8_cow& other) = default;
    utf8_cow(utf8_cow&& other) noexcept = default;
    utf8_cow& operator=(const utf8_cow& other) = default;
    utf8_cow& operator=(utf8_cow&& other) noexcept = default;

    bool is_owned() const {
        return data_.index() == 2 || data_.index() == 3;
    }
    bool is_borrowed() const {
        return data_.index() == 0 || data_.index() == 1;
    }

    size_t length() const {
        return std::visit([](auto&& arg) -> size_t {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_pointer_v<T>) {
                return arg ? arg->length() : 0;
            } else {
                return arg.length();
            }
        }, data_);
    }
    
    uint32_t operator[](size_t index) const {
        return std::visit([index](auto&& arg) -> uint32_t {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_pointer_v<T>) {
                return arg ? (*arg)[index] : 0;
            } else {
                return arg[index];
            }
        }, data_);
    }

    // Access owned string mutably (triggers copy if borrowed or rope)
    utf8_string& get_mut_string() {
        if (data_.index() == 0) {
            auto* p = std::get<0>(data_);
            data_ = p ? utf8_string(*p) : utf8_string();
        } else if (data_.index() == 1) {
            auto* p = std::get<1>(data_);
            data_ = p ? p->to_utf8_string() : utf8_string();
        } else if (data_.index() == 3) {
            data_ = std::get<3>(data_).to_utf8_string();
        }
        return std::get<2>(data_);
    }

    // Access owned rope mutably (triggers copy if borrowed or string)
    utf8_rope& get_mut_rope() {
        if (data_.index() == 1) {
            auto* p = std::get<1>(data_);
            data_ = p ? utf8_rope(*p) : utf8_rope();
        } else if (data_.index() == 0) {
            auto* p = std::get<0>(data_);
            data_ = p ? utf8_rope(*p) : utf8_rope();
        } else if (data_.index() == 2) {
            data_ = utf8_rope(std::get<2>(data_));
        }
        return std::get<3>(data_);
    }
    
    void push_back(uint32_t cp) {
        if (data_.index() == 1 || data_.index() == 3) {
            utf8_string str;
            str.push_back(cp);
            data_ = get_mut_rope() + utf8_rope(str);
        } else {
            get_mut_string().push_back(cp);
        }
    }
    
    utf8_slice slice(size_t start = 0, size_t len = std::string::npos) const {
        return std::visit([start, len](auto&& arg) -> utf8_slice {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_pointer_v<T>) {
                if (!arg) return utf8_slice();
                return utf8_slice(*arg, start, len);
            } else {
                return utf8_slice(arg, start, len);
            }
        }, data_);
    }
};

inline std::ostream& operator<<(std::ostream& os, const utf8_cow& cow) {
    return os << cow.slice();
}

inline std::istream& operator>>(std::istream& is, utf8_cow& cow) {
    utf8_string temp;
    if (is >> temp) {
        cow = utf8_cow(std::move(temp));
    }
    return is;
}

} // namespace kvasir
