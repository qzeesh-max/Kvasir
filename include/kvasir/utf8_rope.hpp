#pragma once

#include <memory>
#include <string>
#include <string_view>
#include "utf8_string.hpp"
#include "hash.hpp"
namespace kvasir {

    template<typename T>
    class intrusive_ptr {
        T* ptr_ = nullptr;
    public:
        intrusive_ptr() = default;
        intrusive_ptr(T* p) : ptr_(p) {
            if (ptr_) ptr_->ref_count.fetch_add(1, std::memory_order_relaxed);
        }
        intrusive_ptr(const intrusive_ptr& other) : ptr_(other.ptr_) {
            if (ptr_) ptr_->ref_count.fetch_add(1, std::memory_order_relaxed);
        }
        intrusive_ptr(intrusive_ptr&& other) noexcept : ptr_(other.ptr_) {
            other.ptr_ = nullptr;
        }
        ~intrusive_ptr() {
            if (ptr_ && ptr_->ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                delete ptr_;
            }
        }
        intrusive_ptr& operator=(const intrusive_ptr& other) {
            if (this != &other) {
                if (ptr_ && ptr_->ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) delete ptr_;
                ptr_ = other.ptr_;
                if (ptr_) ptr_->ref_count.fetch_add(1, std::memory_order_relaxed);
            }
            return *this;
        }
        intrusive_ptr& operator=(intrusive_ptr&& other) noexcept {
            if (this != &other) {
                if (ptr_ && ptr_->ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) delete ptr_;
                ptr_ = other.ptr_;
                other.ptr_ = nullptr;
            }
            return *this;
        }
        T* operator->() const { return ptr_; }
        T& operator*() const { return *ptr_; }
        explicit operator bool() const { return ptr_ != nullptr; }
        T* get() const { return ptr_; }
    };

class utf8_rope {
private:
    struct node {
        mutable std::atomic<uint32_t> ref_count{0};
        size_t cp_length;
        size_t byte_length;
        
        intrusive_ptr<node> left;
        intrusive_ptr<node> right;
        
        kvasir::utf8_string data;
        
        explicit node(const kvasir::utf8_string& str) 
            : cp_length(str.length()), byte_length(str.internal_data().size()), data(str) {}
            
        node(intrusive_ptr<node> l, intrusive_ptr<node> r)
            : cp_length((l ? l->cp_length : 0) + (r ? r->cp_length : 0)),
              byte_length((l ? l->byte_length : 0) + (r ? r->byte_length : 0)),
              left(std::move(l)), right(std::move(r)) {}
    };

    intrusive_ptr<node> root_;

    utf8_rope(intrusive_ptr<node> r) : root_(std::move(r)) {}

    static std::pair<intrusive_ptr<node>, intrusive_ptr<node>> split_node(intrusive_ptr<node> n, size_t pos) {
        if (!n) return {nullptr, nullptr};
        
        if (!n->left && !n->right) {
            utf8_string s1 = n->data.substr(0, pos);
            utf8_string s2 = n->data.substr(pos);
            return {
                s1.empty() ? nullptr : new node(s1),
                s2.empty() ? nullptr : new node(s2)
            };
        }
        
        size_t left_len = n->left ? n->left->cp_length : 0;
        if (pos == left_len) {
            return {n->left, n->right};
        } else if (pos < left_len) {
            auto [ll, lr] = split_node(n->left, pos);
            intrusive_ptr<node> new_right = nullptr;
            if (lr && n->right) new_right = new node(lr, n->right);
            else if (lr) new_right = lr;
            else new_right = n->right;
            return {ll, new_right};
        } else {
            auto [rl, rr] = split_node(n->right, pos - left_len);
            intrusive_ptr<node> new_left = nullptr;
            if (n->left && rl) new_left = new node(n->left, rl);
            else if (rl) new_left = rl;
            else new_left = n->left;
            return {new_left, rr};
        }
    }

    void collect_strings(node* n, std::string& out) const {
        if (!n) return;
        if (!n->left && !n->right) {
            out.append(n->data.internal_data());
        } else {
            collect_strings(n->left.get(), out);
            collect_strings(n->right.get(), out);
        }
    }

    void hash_rope_node(node* n, size_t& current_hash) const {
        if (!n) return;
        if (!n->left && !n->right) {
            current_hash = kvasir::detail::fnv1a_append(current_hash, n->data.internal_data().data(), n->data.internal_data().size());
        } else {
            hash_rope_node(n->left.get(), current_hash);
            hash_rope_node(n->right.get(), current_hash);
        }
    }

public:
    utf8_rope() : root_(nullptr) {}
    
    explicit utf8_rope(std::string_view str) {
        if (!str.empty()) {
            root_ = new node(kvasir::utf8_string(str));
        }
    }
    
    explicit utf8_rope(const char* str) {
        if (str && *str) {
            root_ = new node(kvasir::utf8_string(str));
        }
    }
    
    explicit utf8_rope(const utf8_string& str) {
        if (!str.empty()) {
            root_ = new node(str);
        }
    }
    
    size_t length() const { return root_ ? root_->cp_length : 0; }
    size_t byte_size() const { return root_ ? root_->byte_length : 0; }
    
    uint32_t operator[](size_t index) const {
        if (index >= length()) return 0; // or throw std::out_of_range
        
        node* curr = root_.get();
        while (curr->left || curr->right) {
            size_t left_len = curr->left ? curr->left->cp_length : 0;
            if (index < left_len) {
                curr = curr->left.get();
            } else {
                index -= left_len;
                curr = curr->right.get();
            }
        }
        return curr->data[index];
    }
    
    std::string to_string() const {
        std::string res;
        res.reserve(byte_size());
        collect_strings(root_.get(), res);
        return res;
    }

    size_t hash() const {
        size_t h = kvasir::detail::fnv1a_offset_basis;
        hash_rope_node(root_.get(), h);
        return h;
    }
    
    utf8_string to_utf8_string() const {
        return utf8_string(to_string());
    }

    friend utf8_rope operator+(const utf8_rope& lhs, const utf8_rope& rhs) {
        if (!lhs.root_) return rhs;
        if (!rhs.root_) return lhs;
        return utf8_rope(new node(lhs.root_, rhs.root_));
    }

    utf8_rope& operator+=(const utf8_rope& other) {
        *this = *this + other;
        return *this;
    }

    std::pair<utf8_rope, utf8_rope> split(size_t pos) const {
        if (pos == 0) return {utf8_rope(), *this};
        if (pos >= length()) return {*this, utf8_rope()};
        
        auto [l_root, r_root] = split_node(root_, pos);
        return {utf8_rope(l_root), utf8_rope(r_root)};
    }

    utf8_rope insert(size_t pos, const utf8_rope& other) const {
        if (pos == 0) return other + *this;
        if (pos >= length()) return *this + other;
        
        auto [left, right] = split(pos);
        return left + other + right;
    }
    
    utf8_rope erase(size_t pos, size_t count = std::string::npos) const {
        if (pos >= length()) return *this;
        if (count == 0) return *this;
        if (count == std::string::npos || pos + count >= length()) count = length() - pos;
        
        auto [left, rem] = split(pos);
        auto [mid, right] = rem.split(count);
        return left + right;
    }
    
    size_t find(const utf8_string& str, size_t pos = 0) const {
        return to_utf8_string().find(str, pos);
    }
    
    size_t find(const utf8_rope& rope, size_t pos = 0) const {
        return to_utf8_string().find(rope.to_utf8_string(), pos);
    }
};

inline bool operator==(const utf8_rope& lhs, const utf8_rope& rhs) { return lhs.to_string() == rhs.to_string(); }
inline bool operator!=(const utf8_rope& lhs, const utf8_rope& rhs) { return lhs.to_string() != rhs.to_string(); }
inline bool operator<(const utf8_rope& lhs, const utf8_rope& rhs) { return lhs.to_string() < rhs.to_string(); }

} // namespace kvasir

namespace std {

template <>
struct hash<kvasir::utf8_rope> {
    using is_transparent = void;

    size_t operator()(const kvasir::utf8_rope& rope) const {
        return rope.hash();
    }

    size_t operator()(const kvasir::utf8_string& str) const {
        return kvasir::detail::fnv1a_hash(str.internal_data().data(), str.internal_data().size());
    }

    size_t operator()(std::string_view str) const {
        return kvasir::detail::fnv1a_hash(str.data(), str.size());
    }
};

} // namespace std
