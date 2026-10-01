#pragma once

#include <memory>
#include <string>
#include <string_view>
#include "utf8_string.hpp"
#include <iostream>
#include "hash.hpp"
#include <atomic>
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

        // ── Lock-free two-level pool ─────────────────────────────────────────
        // Fast path  : per-thread singly-linked list, no atomics.
        // Overflow   : when the local list exceeds kDonateThreshold nodes, half
        //              are pushed onto a global Treiber stack so other threads
        //              (including those freeing cross-thread nodes) can reclaim them.
        // Thread exit: pool_state destructor drains the local list into the global
        //              stack, preventing permanent per-thread memory accumulation.
        // ────────────────────────────────────────────────────────────────────

        static constexpr size_t kDonateThreshold = 512;
        static constexpr size_t kStealBatch       = 16;

        // Global lock-free Treiber stack shared across all threads.
        static std::atomic<void*>& global_pool() noexcept {
            static std::atomic<void*> pool{nullptr};
            return pool;
        }

        static void treiber_push(void* p) noexcept {
            auto& gp = global_pool();
            void* head = gp.load(std::memory_order_relaxed);
            do {
                *reinterpret_cast<void**>(p) = head;
            } while (!gp.compare_exchange_weak(
                         head, p,
                         std::memory_order_release,
                         std::memory_order_relaxed));
        }

        static void* treiber_pop() noexcept {
            auto& gp = global_pool();
            void* head = gp.load(std::memory_order_acquire);
            while (head) {
                void* next = *reinterpret_cast<void**>(head);
                if (gp.compare_exchange_weak(
                        head, next,
                        std::memory_order_acquire,
                        std::memory_order_relaxed))
                    return head;
            }
            return nullptr;
        }

        // Per-thread cache state + destructor-based donation on thread exit.
        struct pool_state {
            void*  fast_list  = nullptr;
            size_t fast_count = 0;

            ~pool_state() noexcept {
                // Donate all remaining local nodes to the global pool on exit.
                while (fast_list) {
                    void* next = *reinterpret_cast<void**>(fast_list);
                    treiber_push(fast_list);
                    fast_list = next;
                }
            }

            static pool_state& get() noexcept {
                thread_local pool_state ps;
                return ps;
            }
        };

        static void* operator new(size_t size) {
            if (size != sizeof(node)) return ::operator new(size);
            auto& ps = pool_state::get();

            // 1. Fast path: pop from local list.
            if (ps.fast_list) {
                void* p = ps.fast_list;
                ps.fast_list = *reinterpret_cast<void**>(p);
                --ps.fast_count;
                return p;
            }

            // 2. Steal a batch from the global Treiber stack.
            void* first = treiber_pop();
            if (first) {
                for (size_t i = 1; i < kStealBatch; ++i) {
                    void* q = treiber_pop();
                    if (!q) break;
                    *reinterpret_cast<void**>(q) = ps.fast_list;
                    ps.fast_list = q;
                    ++ps.fast_count;
                }
                return first;
            }

            // 3. Allocate a fresh chunk from the OS.
            const int chunk_size = 256;
            void* chunk = ::operator new(chunk_size * sizeof(node));
            for (int i = 1; i < chunk_size; ++i) {
                void* item = static_cast<char*>(chunk) + i * sizeof(node);
                *reinterpret_cast<void**>(item) = ps.fast_list;
                ps.fast_list = item;
            }
            ps.fast_count += chunk_size - 1;
            return chunk;
        }

        static void operator delete(void* p, size_t size) {
            if (!p) return;
            if (size != sizeof(node)) { ::operator delete(p); return; }

            auto& ps = pool_state::get();
            *reinterpret_cast<void**>(p) = ps.fast_list;
            ps.fast_list = p;
            ++ps.fast_count;

            // Donate half to global pool when local cache is too large.
            if (ps.fast_count > kDonateThreshold) {
                size_t to_donate = ps.fast_count / 2;
                for (size_t i = 0; i < to_donate; ++i) {
                    void* q = ps.fast_list;
                    ps.fast_list = *reinterpret_cast<void**>(q);
                    --ps.fast_count;
                    treiber_push(q);
                }
            }
        }
        
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

    void stream_node(node* n, std::ostream& os) const {
        if (!n) return;
        if (!n->left && !n->right) {
            os << n->data;
        } else {
            stream_node(n->left.get(), os);
            stream_node(n->right.get(), os);
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

    friend std::ostream& operator<<(std::ostream& os, const utf8_rope& rope);
    
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

    friend utf8_rope operator+(utf8_rope&& lhs, const utf8_rope& rhs) {
        if (!lhs.root_) return rhs;
        if (!rhs.root_) return std::move(lhs);
        
        if (lhs.root_->ref_count.load(std::memory_order_relaxed) == 1) {
            if (!lhs.root_->left && !lhs.root_->right) {
                if (!rhs.root_->left && !rhs.root_->right && lhs.root_->byte_length + rhs.root_->byte_length <= 512) {
                    lhs.root_->data += rhs.root_->data;
                    lhs.root_->cp_length += rhs.root_->cp_length;
                    lhs.root_->byte_length += rhs.root_->byte_length;
                    return std::move(lhs);
                }
            }
        }
        
        return utf8_rope(new node(lhs.root_, rhs.root_));
    }

    utf8_rope& operator+=(const utf8_rope& other) {
        *this = std::move(*this) + other;
        return *this;
    }

    std::pair<utf8_rope, utf8_rope> split(size_t pos) const {
        if (pos == 0) return {utf8_rope(), *this};
        if (pos >= length()) return {*this, utf8_rope()};
        
        auto [l_root, r_root] = split_node(root_, pos);
        return {utf8_rope(l_root), utf8_rope(r_root)};
    }

    // Fast append: bypass split entirely when adding at end
    utf8_rope& append(const utf8_rope& other) {
        *this = std::move(*this) + other;
        return *this;
    }

    utf8_rope insert(size_t pos, const utf8_rope& other) const & {
        if (pos == 0) return other + *this;
        if (pos >= length()) return *this + other;
        
        auto [left, right] = split(pos);
        return left + other + right;
    }
    
    utf8_rope insert(size_t pos, const utf8_rope& other) && {
        if (pos == 0) return other + std::move(*this);
        if (pos >= length()) return std::move(*this) + other;
        
        auto [left, right] = split(pos);
        return std::move(left) + other + std::move(right);
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

inline std::ostream& operator<<(std::ostream& os, const utf8_rope& rope) {
    rope.stream_node(rope.root_.get(), os);
    return os;
}

inline std::istream& operator>>(std::istream& is, utf8_rope& rope) {
    utf8_string temp;
    if (is >> temp) {
        rope = utf8_rope(temp);
    }
    return is;
}

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
