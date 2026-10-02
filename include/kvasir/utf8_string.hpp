#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include "hash.hpp"
#if __has_include(<experimental/simd>)
#include <experimental/simd>
#endif
#include <string_view>
#include <functional>
#include <iostream>
namespace kvasir {

constexpr size_t kCacheLineSize = 64;
constexpr size_t kFragmentDiffsSize = kCacheLineSize - sizeof(size_t);
constexpr size_t kCodePointsPerFragment = kFragmentDiffsSize + 1;

struct alignas(kCacheLineSize) CodePointIndexFragment {
    size_t fragmentFirstCharIndex;
    uint8_t fragmentCharIndexDiffs[kFragmentDiffsSize];
};

static constexpr uint8_t utf8_len_table[256] = {
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3, 4,4,4,4,4,4,4,4,5,5,5,5,6,6,6,6
};

class utf8_string {
public:
    utf8_string() : code_point_count_(0), inline_index_{0, {}} {
        data_.append(3, '\0');
    }
    
    utf8_string(const char* str) : code_point_count_(0), inline_index_{0, {}} {
        size_t len = std::strlen(str);
        data_.reserve(len + 3);
        data_.assign(str, len);
        data_.append(3, '\0');
        rebuild_index();
    }
    
    utf8_string(const std::string& str) : code_point_count_(0), inline_index_{0, {}} {
        data_.reserve(str.size() + 3);
        data_.assign(str);
        data_.append(3, '\0');
        rebuild_index();
    }
    
    utf8_string(std::string_view str) : code_point_count_(0), inline_index_{0, {}} {
        data_.reserve(str.size() + 3);
        data_.assign(str.data(), str.size());
        data_.append(3, '\0');
        rebuild_index();
    }

    utf8_string(const utf8_string& other) : data_(other.data_), code_point_count_(other.code_point_count_), inline_index_(other.inline_index_), heap_index_(other.heap_index_) {
    }

    utf8_string(utf8_string&& other) noexcept : data_(std::move(other.data_)), code_point_count_(other.code_point_count_), inline_index_(other.inline_index_), heap_index_(std::move(other.heap_index_)) {
        other.code_point_count_ = 0;
        if (data_.size() < 3) data_.append(3, '\0');
    }

    utf8_string& operator=(const utf8_string& other) {
        if (this != &other) {
            data_ = other.data_;
            code_point_count_ = other.code_point_count_;
            inline_index_ = other.inline_index_;
            heap_index_ = other.heap_index_;
        }
        return *this;
    }

    utf8_string& operator=(utf8_string&& other) noexcept {
        if (this != &other) {
            data_ = std::move(other.data_);
            code_point_count_ = other.code_point_count_;
            inline_index_ = other.inline_index_;
            heap_index_ = std::move(other.heap_index_);
            other.code_point_count_ = 0;
            if (data_.size() < 3) data_.append(3, '\0');
        }
        return *this;
    }



    bool empty() const { return logical_size() == 0; }
    size_t size() const { return code_point_count_; }
    size_t length() const { return code_point_count_; }
    
    // String access
    std::string_view internal_data() const { return std::string_view(data_.data(), logical_size()); }
    const char* c_str() const { return data_.c_str(); }
    class const_iterator {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = uint32_t;
        using difference_type = ptrdiff_t;
        using pointer = const uint32_t*;
        using reference = uint32_t;

        const_iterator() : str_(nullptr), index_(0), byte_idx_(0) {}
        const_iterator(const utf8_string* str, size_t index) : str_(str), index_(index), byte_idx_(str ? str->byte_index(index) : 0) {}

        reference operator*() const { 
            return str_->decode_code_point(byte_idx_);
        }
        
        const_iterator& operator++() { 
            const uint8_t* p = reinterpret_cast<const uint8_t*>(str_->internal_data().data());
            byte_idx_ += utf8_len_table[p[byte_idx_]];
            ++index_; 
            return *this; 
        }
        const_iterator operator++(int) { const_iterator tmp = *this; ++(*this); return tmp; }
        const_iterator& operator--() { 
            --index_; 
            byte_idx_ = str_->byte_index(index_); 
            return *this; 
        }
        const_iterator operator--(int) { const_iterator tmp = *this; --(*this); return tmp; }
        
        const_iterator& operator+=(difference_type n) { 
            index_ += n; 
            byte_idx_ = str_->byte_index(index_); 
            return *this; 
        }
        const_iterator& operator-=(difference_type n) { 
            index_ -= n; 
            byte_idx_ = str_->byte_index(index_); 
            return *this; 
        }
        
        friend const_iterator operator+(const_iterator it, difference_type n) { return const_iterator(it.str_, it.index_ + n); }
        friend const_iterator operator+(difference_type n, const_iterator it) { return const_iterator(it.str_, it.index_ + n); }
        friend const_iterator operator-(const_iterator it, difference_type n) { return const_iterator(it.str_, it.index_ - n); }
        friend difference_type operator-(const const_iterator& lhs, const const_iterator& rhs) { return lhs.index_ - rhs.index_; }
        
        friend bool operator==(const const_iterator& lhs, const const_iterator& rhs) { return lhs.index_ == rhs.index_; }
        friend bool operator!=(const const_iterator& lhs, const const_iterator& rhs) { return lhs.index_ != rhs.index_; }
        friend bool operator<(const const_iterator& lhs, const const_iterator& rhs) { return lhs.index_ < rhs.index_; }
        friend bool operator>(const const_iterator& lhs, const const_iterator& rhs) { return lhs.index_ > rhs.index_; }
        friend bool operator<=(const const_iterator& lhs, const const_iterator& rhs) { return lhs.index_ <= rhs.index_; }
        friend bool operator>=(const const_iterator& lhs, const const_iterator& rhs) { return lhs.index_ >= rhs.index_; }

    private:
        const utf8_string* str_;
        size_t index_;
        size_t byte_idx_;
    };


    const_iterator begin() const { return const_iterator(this, 0); }
    const_iterator end() const { return const_iterator(this, code_point_count_); }

    void push_back(uint32_t cp) {
        // Encode cp into a small stack buffer so we know cp_len before
        // touching data_ — avoids the old erase(size-3)+push_back+append
        // pattern which triggered an O(n) memmove on every call.
        char encoded[4];
        size_t cp_len;
        if (cp <= 0x7F) {
            encoded[0] = static_cast<char>(cp); cp_len = 1;
        } else if (cp <= 0x7FF) {
            encoded[0] = static_cast<char>(0xC0 | ((cp >> 6) & 0x1F));
            encoded[1] = static_cast<char>(0x80 | (cp & 0x3F)); cp_len = 2;
        } else if (cp <= 0xFFFF) {
            if (cp >= 0xD800 && cp <= 0xDFFF) return; // invalid surrogate
            encoded[0] = static_cast<char>(0xE0 | ((cp >> 12) & 0x0F));
            encoded[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            encoded[2] = static_cast<char>(0x80 | (cp & 0x3F)); cp_len = 3;
        } else if (cp <= 0x10FFFF) {
            encoded[0] = static_cast<char>(0xF0 | ((cp >> 18) & 0x07));
            encoded[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            encoded[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            encoded[3] = static_cast<char>(0x80 | (cp & 0x3F)); cp_len = 4;
        } else {
            return; // invalid code point — silently ignore
        }

        // Buffer layout: [payload (logical_size bytes) | \0\0\0 (3 sentinel bytes)]
        // We overwrite the sentinel in-place and extend by cp_len bytes net.
        // This avoids the memmove that data_.erase(size-3) would cause.
        size_t prev_byte_idx = logical_size();
        data_.resize(prev_byte_idx + cp_len + 3);
        std::memcpy(data_.data() + prev_byte_idx, encoded, cp_len);
        // Re-zero the three sentinel bytes (resize may already zero them on
        // grow, but be explicit for correctness).
        data_[prev_byte_idx + cp_len]     = '\0';
        data_[prev_byte_idx + cp_len + 1] = '\0';
        data_[prev_byte_idx + cp_len + 2] = '\0';

        size_t local_idx = code_point_count_ % kCodePointsPerFragment;

        if (local_idx == 0) {
            CodePointIndexFragment frag;
            frag.fragmentFirstCharIndex = prev_byte_idx;
            std::memset(frag.fragmentCharIndexDiffs, 0, kFragmentDiffsSize);
            frag.fragmentCharIndexDiffs[0] = static_cast<uint8_t>(cp_len);
            add_fragment(frag, code_point_count_ / kCodePointsPerFragment);
        } else {
            auto& frag = get_mutable_fragment(code_point_count_ / kCodePointsPerFragment);
            if (local_idx < kFragmentDiffsSize) {
                frag.fragmentCharIndexDiffs[local_idx] = static_cast<uint8_t>(
                    (prev_byte_idx + cp_len) - frag.fragmentFirstCharIndex);
            }
        }
        code_point_count_++;
    }

    utf8_string& append(const char* str) {
        size_t old_logical = logical_size();
        size_t old_cp = code_point_count_;
        size_t add_len = std::strlen(str);
        // Overwrite sentinel in-place (same trick as push_back) — no memmove.
        data_.resize(old_logical + add_len + 3);
        std::memcpy(data_.data() + old_logical, str, add_len);
        data_[old_logical + add_len]     = '\0';
        data_[old_logical + add_len + 1] = '\0';
        data_[old_logical + add_len + 2] = '\0';
        append_index(old_logical, old_cp);
        return *this;
    }

    utf8_string& append(const std::string& str) {
        size_t old_logical = logical_size();
        size_t old_cp = code_point_count_;
        size_t add_len = str.size();
        data_.resize(old_logical + add_len + 3);
        std::memcpy(data_.data() + old_logical, str.data(), add_len);
        data_[old_logical + add_len]     = '\0';
        data_[old_logical + add_len + 1] = '\0';
        data_[old_logical + add_len + 2] = '\0';
        append_index(old_logical, old_cp);
        return *this;
    }

    utf8_string& append(const utf8_string& str) {
        size_t old_logical = logical_size();
        size_t old_cp = code_point_count_;
        auto sv = str.internal_data();
        size_t add_len = sv.size();
        data_.resize(old_logical + add_len + 3);
        std::memcpy(data_.data() + old_logical, sv.data(), add_len);
        data_[old_logical + add_len]     = '\0';
        data_[old_logical + add_len + 1] = '\0';
        data_[old_logical + add_len + 2] = '\0';
        append_index(old_logical, old_cp);
        return *this;
    }

    utf8_string& operator+=(const char* str) { return append(str); }
    utf8_string& operator+=(const std::string& str) { return append(str); }
    utf8_string& operator+=(const utf8_string& str) { return append(str); }
    
    // Substring
    utf8_string substr(size_t pos = 0, size_t count = std::string::npos) const {
        if (pos >= code_point_count_) return utf8_string();
        
        size_t byte_pos = byte_index(pos);
        size_t byte_end;
        if (count == std::string::npos || pos + count >= code_point_count_) {
            byte_end = logical_size();
        } else {
            byte_end = byte_index(pos + count);
        }
        
        return utf8_string(data_.substr(byte_pos, byte_end - byte_pos));
    }
    
    // Compare
    int compare(const utf8_string& other) const {
        return internal_data().compare(other.internal_data());
    }
    int compare(const std::string& other) const {
        return internal_data().compare(other);
    }
    
    // Find
    size_t find(const utf8_string& str, size_t pos = 0) const {
        if (pos > code_point_count_) return std::string::npos;
        size_t byte_pos = (pos == code_point_count_) ? logical_size() : byte_index(pos);
        size_t found_byte = internal_data().find(str.internal_data(), byte_pos);
        if (found_byte == std::string::npos) return std::string::npos;
        return code_point_index(found_byte);
    }
    
    void swap(utf8_string& other) noexcept {
        data_.swap(other.data_);
        std::swap(code_point_count_, other.code_point_count_);
        std::swap(inline_index_, other.inline_index_);
        heap_index_.swap(other.heap_index_);
    }
    
    // Insert
    utf8_string& insert(size_t pos, const utf8_string& str) {
        if (pos > code_point_count_) return *this;
        if (pos == code_point_count_) return append(str);

        size_t byte_pos = byte_index(pos);
        // std::string::insert is unavoidably O(n) for the data; we still
        // avoid rescanning the prefix by rebuilding only from byte_pos.
        data_.insert(byte_pos, str.internal_data());
        partial_rebuild_index_from(pos, byte_pos);
        return *this;
    }

    // Erase
    utf8_string& erase(size_t pos = 0, size_t count = std::string::npos) {
        if (pos >= code_point_count_) return *this;
        size_t byte_pos = byte_index(pos);
        size_t byte_end;
        if (count == std::string::npos || pos + count >= code_point_count_) {
            // Erasing to end: just truncate — no rescan at all.
            // Re-zero the 3-byte sentinel at the new end.
            data_.resize(byte_pos + 3);
            data_[byte_pos]     = '\0';
            data_[byte_pos + 1] = '\0';
            data_[byte_pos + 2] = '\0';
            code_point_count_ = pos;
            // Trim any heap fragments past pos.
            size_t new_frag_count = (pos == 0) ? 1 :
                pos / kCodePointsPerFragment + (pos % kCodePointsPerFragment ? 1 : 0);
            if (!heap_index_.empty() && heap_index_.size() > new_frag_count)
                heap_index_.resize(new_frag_count);
            return *this;
        }
        byte_end = byte_index(pos + count);
        data_.erase(byte_pos, byte_end - byte_pos);
        partial_rebuild_index_from(pos, byte_pos);
        return *this;
    }

    // Replace
    utf8_string& replace(size_t pos, size_t count, const utf8_string& str) {
        if (pos >= code_point_count_) return *this;
        size_t byte_pos = byte_index(pos);
        size_t byte_end;
        if (count == std::string::npos || pos + count >= code_point_count_) {
            byte_end = logical_size();
        } else {
            byte_end = byte_index(pos + count);
        }
        data_.replace(byte_pos, byte_end - byte_pos, str.internal_data());
        partial_rebuild_index_from(pos, byte_pos);
        return *this;
    }
    
    // Batch decoding using SIMD for fast paths
    size_t decode_code_points(size_t cp_pos, size_t count, uint32_t* out) const {
        if (cp_pos >= code_point_count_) return 0;
        if (cp_pos + count > code_point_count_) count = code_point_count_ - cp_pos;

        size_t byte_idx  = byte_index(cp_pos);
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data_.data());
        // Cache logical_size() — avoids data_.size()-3 subtraction per iteration.
        const size_t lsize = logical_size();
        size_t cp_decoded  = 0;

#if __has_include(<experimental/simd>)
        using simd8_t  = std::experimental::fixed_size_simd<int8_t,  16>;
        using simd32_t = std::experimental::fixed_size_simd<uint32_t, 16>;
        constexpr size_t kSimdWidth = 16;
#endif

        while (cp_decoded < count) {
            // Prefetch ahead to hide memory latency on large sequential decodes.
            __builtin_prefetch(p + byte_idx + 64, 0, 1);

#if __has_include(<experimental/simd>)
            if (__builtin_expect(cp_decoded + kSimdWidth <= count &&
                                 byte_idx + kSimdWidth <= lsize, 1)) {
                simd8_t chunk(reinterpret_cast<const int8_t*>(p + byte_idx),
                              std::experimental::element_aligned);
                if (!std::experimental::any_of(chunk < 0)) {
                    simd32_t expanded =
                        std::experimental::static_simd_cast<uint32_t>(chunk);
                    expanded.copy_to(out + cp_decoded,
                                     std::experimental::element_aligned);
                    byte_idx   += kSimdWidth;
                    cp_decoded += kSimdWidth;
                    continue;
                }
            }
#else
            // 8-byte scalar ASCII fast path (no SIMD available).
            if (__builtin_expect(cp_decoded + 8 <= count &&
                                 byte_idx + 8   <= lsize, 1)) {
                uint64_t chunk;
                std::memcpy(&chunk, p + byte_idx, 8);
                if ((chunk & 0x8080808080808080ULL) == 0) {
                    // All 8 bytes are ASCII: expand each to uint32_t.
                    out[cp_decoded + 0] = static_cast<uint8_t>(p[byte_idx + 0]);
                    out[cp_decoded + 1] = static_cast<uint8_t>(p[byte_idx + 1]);
                    out[cp_decoded + 2] = static_cast<uint8_t>(p[byte_idx + 2]);
                    out[cp_decoded + 3] = static_cast<uint8_t>(p[byte_idx + 3]);
                    out[cp_decoded + 4] = static_cast<uint8_t>(p[byte_idx + 4]);
                    out[cp_decoded + 5] = static_cast<uint8_t>(p[byte_idx + 5]);
                    out[cp_decoded + 6] = static_cast<uint8_t>(p[byte_idx + 6]);
                    out[cp_decoded + 7] = static_cast<uint8_t>(p[byte_idx + 7]);
                    byte_idx   += 8;
                    cp_decoded += 8;
                    continue;
                }
            }
#endif
            uint8_t c0 = p[byte_idx];
            if (c0 <= 0x7F) {
                out[cp_decoded] = c0;
            } else {
                uint32_t c1 = p[byte_idx + 1];
                if (c0 <= 0xDF) {
                    out[cp_decoded] = ((c0 & 0x1F) << 6) | (c1 & 0x3F);
                } else {
                    uint32_t c2 = p[byte_idx + 2];
                    if (c0 <= 0xEF) {
                        out[cp_decoded] = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
                    } else {
                        uint32_t c3 = p[byte_idx + 3];
                        out[cp_decoded] = ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
                    }
                }
            }
            byte_idx += utf8_len_table[c0];
            cp_decoded++;
        }
        return cp_decoded;
    }

    uint32_t operator[](size_t index) const {
        if (index >= code_point_count_) return 0; // or throw
        size_t fragment_idx = index / kCodePointsPerFragment;
        size_t local_idx = index % kCodePointsPerFragment;
        const auto& frag = get_fragment(fragment_idx);
        
        size_t byte_idx = local_idx == 0 ? frag.fragmentFirstCharIndex :
                          frag.fragmentFirstCharIndex + frag.fragmentCharIndexDiffs[local_idx - 1];
        
        return decode_code_point(byte_idx);
    }

private:
    friend class const_iterator;

    std::string data_;
    
    // SSO for fragments
    CodePointIndexFragment inline_index_;
    std::vector<CodePointIndexFragment> heap_index_;
    
    size_t code_point_count_ = 0;

    size_t logical_size() const {
        return data_.size() >= 3 ? data_.size() - 3 : 0;
    }

    size_t byte_index(size_t cp_idx) const {
        if (cp_idx == 0) return 0;
        if (cp_idx >= code_point_count_) return logical_size();
        
        size_t fragment_idx = cp_idx / kCodePointsPerFragment;
        size_t local_idx = cp_idx % kCodePointsPerFragment;
        const auto& frag = get_fragment(fragment_idx);
        
        if (local_idx == 0) return frag.fragmentFirstCharIndex;
        return frag.fragmentFirstCharIndex + frag.fragmentCharIndexDiffs[local_idx - 1];
    }
    
    size_t code_point_index(size_t byte_idx) const {
        // Binary search for fragment
        size_t count = code_point_count_ / kCodePointsPerFragment + (code_point_count_ % kCodePointsPerFragment ? 1 : 0);
        if (count == 0) return 0;
        
        size_t low = 0;
        size_t high = count - 1;
        size_t frag_idx = 0;
        
        while (low <= high) {
            size_t mid = low + (high - low) / 2;
            const auto& frag = get_fragment(mid);
            if (frag.fragmentFirstCharIndex <= byte_idx) {
                frag_idx = mid;
                low = mid + 1;
            } else {
                if (mid == 0) break;
                high = mid - 1;
            }
        }
        
        const auto& frag = get_fragment(frag_idx);
        size_t cp_idx = frag_idx * kCodePointsPerFragment;
        if (frag.fragmentFirstCharIndex == byte_idx) return cp_idx;
        
        // Scan inside fragment
        size_t max_local = (frag_idx == count - 1) ? (code_point_count_ % kCodePointsPerFragment) : kCodePointsPerFragment;
        if (max_local == 0) max_local = kCodePointsPerFragment;
        
        for (size_t i = 0; i < max_local - 1; i++) {
            if (frag.fragmentFirstCharIndex + frag.fragmentCharIndexDiffs[i] >= byte_idx) {
                return cp_idx + i + 1;
            }
        }
        return cp_idx + max_local - 1;
    }

    const CodePointIndexFragment& get_fragment(size_t index) const {
        return (code_point_count_ <= kCodePointsPerFragment) ? inline_index_ : heap_index_[index];
    }
    
    CodePointIndexFragment& get_mutable_fragment(size_t index) {
        return (code_point_count_ <= kCodePointsPerFragment) ? inline_index_ : heap_index_[index];
    }


    void add_fragment(const CodePointIndexFragment& frag, size_t frag_idx, size_t reserve_capacity = 0) {
        if (frag_idx == 0) {
            inline_index_ = frag;
        } else {
            if (heap_index_.empty()) {
                if (reserve_capacity > 0) heap_index_.reserve(reserve_capacity);
                heap_index_.push_back(inline_index_);
            }
            if (heap_index_.size() == frag_idx) {
                heap_index_.push_back(frag);
            } else if (heap_index_.size() > frag_idx) {
                heap_index_[frag_idx] = frag;
            }
        }
    }
    
    // ─── Core index-building kernel ─────────────────────────────────────────
    // Scans [byte_idx, logical_size()) and builds/updates fragment records
    // starting at code-point position cp_count (== the position of the first
    // byte passed in).  Writes code_point_count_ on completion.
    //
    // The caller is responsible for:
    //   - clearing heap_index_ (if doing a full rebuild)
    //   - passing correct byte_idx and cp_count
    //   - pre-loading the fragment at frag_idx when local_idx > 0
    void build_index_from(size_t byte_idx, size_t cp_count) {
        size_t data_size = logical_size();
        if (byte_idx > data_size) { code_point_count_ = cp_count; return; }

        const uint8_t* p = reinterpret_cast<const uint8_t*>(data_.data());
        const size_t   reserve_hint = data_size / kCodePointsPerFragment + 2;

        size_t local_idx = cp_count % kCodePointsPerFragment;
        size_t frag_idx  = cp_count / kCodePointsPerFragment;

        CodePointIndexFragment frag;
        if (local_idx > 0) {
            // Resume mid-fragment: load existing fragment.
            frag = get_fragment(frag_idx);
        } else {
            frag.fragmentFirstCharIndex = byte_idx;
            std::memset(frag.fragmentCharIndexDiffs, 0, kFragmentDiffsSize);
        }

        while (byte_idx < data_size) {
            if (__builtin_expect(local_idx == kCodePointsPerFragment, 0)) {
                add_fragment(frag, frag_idx++, reserve_hint);
                frag.fragmentFirstCharIndex = byte_idx;
                std::memset(frag.fragmentCharIndexDiffs, 0, kFragmentDiffsSize);
                local_idx = 0;
            }

            // ── 16-byte ASCII fast path ──────────────────────────────────────
            // Two consecutive 8-byte reads; both must be all-ASCII.
            if (__builtin_expect(
                    local_idx + 16 <= kCodePointsPerFragment &&
                    byte_idx + 16  <= data_size, 1)) {
                uint64_t lo, hi;
                std::memcpy(&lo, p + byte_idx,     8);
                std::memcpy(&hi, p + byte_idx + 8, 8);
                if (((lo | hi) & 0x8080808080808080ULL) == 0) {
                    size_t base = byte_idx - frag.fragmentFirstCharIndex;
                    // Unrolled: write diffs for 16 ASCII code-points.
                    uint8_t* d = frag.fragmentCharIndexDiffs + local_idx;
                    // Only write within the diff array (kFragmentDiffsSize = 56 on 64-byte cache line)
                    const size_t avail = kFragmentDiffsSize - local_idx;
                    const size_t n = (avail < 16) ? avail : 16;
                    for (size_t i = 0; i < n; ++i)
                        d[i] = static_cast<uint8_t>(base + i + 1);
                    byte_idx   += 16;
                    local_idx  += 16;
                    cp_count   += 16;
                    continue;
                }
            }
            // ── 8-byte ASCII fast path (tail / non-multiple of 16) ───────────
            if (__builtin_expect(
                    local_idx + 8 <= kCodePointsPerFragment &&
                    byte_idx + 8  <= data_size, 1)) {
                uint64_t chunk;
                std::memcpy(&chunk, p + byte_idx, 8);
                if ((chunk & 0x8080808080808080ULL) == 0) {
                    size_t base = byte_idx - frag.fragmentFirstCharIndex;
                    uint8_t* d = frag.fragmentCharIndexDiffs + local_idx;
                    const size_t avail = kFragmentDiffsSize - local_idx;
                    const size_t n = (avail < 8) ? avail : 8;
                    for (size_t i = 0; i < n; ++i)
                        d[i] = static_cast<uint8_t>(base + i + 1);
                    byte_idx  += 8;
                    local_idx += 8;
                    cp_count  += 8;
                    continue;
                }
            }

            // ── Scalar multibyte path ────────────────────────────────────────
            size_t cp_len   = utf8_len_table[p[byte_idx]];
            size_t remaining = data_size - byte_idx;
            byte_idx += (cp_len < remaining) ? cp_len : remaining;
            if (local_idx < kFragmentDiffsSize)
                frag.fragmentCharIndexDiffs[local_idx] =
                    static_cast<uint8_t>(byte_idx - frag.fragmentFirstCharIndex);
            local_idx++;
            cp_count++;
        }

        code_point_count_ = cp_count;
        if (local_idx > 0 || cp_count == 0)
            add_fragment(frag, frag_idx, reserve_hint);
    }

    void append_index(size_t byte_idx, size_t cp_count) {
        build_index_from(byte_idx, cp_count);
    }

    void rebuild_index() {
        heap_index_.clear();
        code_point_count_ = 0;
        build_index_from(0, 0);
    }

    // Partial index rebuild starting from code-point `cp_pos` (byte `byte_pos`).
    // Fragments 0..frag_of(cp_pos)-1 are left unchanged; only the fragment
    // containing cp_pos and all subsequent ones are rewritten.  This turns
    // O(n) erase/insert/replace rescans into O(suffix) rescans.
    void partial_rebuild_index_from(size_t cp_pos, size_t byte_pos) {
        // Snap back to the fragment boundary so build_index_from gets a
        // clean (local_idx == 0) start rather than a mid-fragment resume.
        size_t frag_idx = cp_pos / kCodePointsPerFragment;
        size_t cp_base  = frag_idx * kCodePointsPerFragment;

        // Derive byte_base BEFORE we trim heap_index_:
        // All fragments < frag_idx are still valid at this point.
        size_t byte_base = (cp_base == 0) ? 0 : byte_index(cp_base);

        // Now trim stale tail fragments.
        if (!heap_index_.empty() && heap_index_.size() > frag_idx)
            heap_index_.resize(frag_idx);
        // If frag_idx == 0, reset the inline fragment so build_index_from
        // starts fresh (otherwise it would try to resume mid-fragment).
        if (frag_idx == 0) {
            inline_index_.fragmentFirstCharIndex = 0;
            std::memset(inline_index_.fragmentCharIndexDiffs, 0, kFragmentDiffsSize);
        }

        build_index_from(byte_base, cp_base);
        (void)byte_pos;
    }

    
    uint32_t decode_code_point(size_t byte_idx) const {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data_.data() + byte_idx);
        uint8_t c0 = p[0];
        if (c0 <= 0x7F) return c0;
        uint32_t c1 = p[1];
        if (c0 <= 0xDF) return ((c0 & 0x1F) << 6) | (c1 & 0x3F);
        uint32_t c2 = p[2];
        if (c0 <= 0xEF) return ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
        uint32_t c3 = p[3];
        return ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
    }

public:
    static bool validate_utf8(std::string_view str) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(str.data());
        size_t size = str.size();
        size_t i = 0;
        
#if __has_include(<experimental/simd>)
        using vsimd8_t = std::experimental::native_simd<int8_t>;
        constexpr size_t kVSimdWidth = vsimd8_t::size();
#endif

        while (i < size) {
#if __has_include(<experimental/simd>)
            if (i + kVSimdWidth <= size) {
                vsimd8_t chunk(reinterpret_cast<const int8_t*>(p + i), std::experimental::element_aligned);
                if (!std::experimental::any_of(chunk < 0)) {
                    i += kVSimdWidth;
                    continue;
                }
            }
#endif
            // Fallback 64-bit ASCII fast path
            if (i + 8 <= size) {
                uint64_t chunk;
                std::memcpy(&chunk, p + i, 8);
                if ((chunk & 0x8080808080808080ULL) == 0) {
                    i += 8;
                    continue;
                }
            }
            uint8_t first = p[i];
            size_t cp_len = utf8_len_table[first];
            if (cp_len == 0 || cp_len > 4 || i + cp_len > size) return false;
            
            if (cp_len == 1) {
                if (first >= 0x80) return false;
            } else if (cp_len == 2) {
                if ((p[i+1] & 0xC0) != 0x80) return false;
                if (first < 0xC2) return false; // Overlong encoding
            } else if (cp_len == 3) {
                if ((p[i+1] & 0xC0) != 0x80 || (p[i+2] & 0xC0) != 0x80) return false;
                if (first == 0xE0 && p[i+1] < 0xA0) return false; // Overlong
                if (first == 0xED && p[i+1] >= 0xA0) return false; // Surrogates
            } else if (cp_len == 4) {
                if (first > 0xF4) return false; // Exceeds U+10FFFF limit
                if ((p[i+1] & 0xC0) != 0x80 || (p[i+2] & 0xC0) != 0x80 || (p[i+3] & 0xC0) != 0x80) return false;
                if (first == 0xF0 && p[i+1] < 0x90) return false; // Overlong
                if (first == 0xF4 && p[i+1] >= 0x90) return false; // Out of bounds
            }
            i += cp_len;
        }
        return true;
    }
};

inline bool operator==(const utf8_string& lhs, const utf8_string& rhs) { return lhs.internal_data() == rhs.internal_data(); }
inline bool operator!=(const utf8_string& lhs, const utf8_string& rhs) { return lhs.internal_data() != rhs.internal_data(); }
inline bool operator<(const utf8_string& lhs, const utf8_string& rhs) { return lhs.internal_data() < rhs.internal_data(); }

inline bool operator==(const utf8_string& lhs, const std::string& rhs) { return lhs.internal_data() == std::string_view(rhs); }
inline bool operator!=(const utf8_string& lhs, const std::string& rhs) { return lhs.internal_data() != std::string_view(rhs); }
inline bool operator<(const utf8_string& lhs, const std::string& rhs) { return lhs.internal_data() < std::string_view(rhs); }

inline bool operator==(const std::string& lhs, const utf8_string& rhs) { return std::string_view(lhs) == rhs.internal_data(); }
inline bool operator!=(const std::string& lhs, const utf8_string& rhs) { return std::string_view(lhs) != rhs.internal_data(); }
inline bool operator<(const std::string& lhs, const utf8_string& rhs) { return std::string_view(lhs) < rhs.internal_data(); }

inline bool operator==(const utf8_string& lhs, const char* rhs) { return lhs.internal_data() == std::string_view(rhs); }
inline bool operator!=(const utf8_string& lhs, const char* rhs) { return lhs.internal_data() != std::string_view(rhs); }
inline bool operator<(const utf8_string& lhs, const char* rhs) { return lhs.internal_data() < std::string_view(rhs); }

inline bool operator==(const char* lhs, const utf8_string& rhs) { return std::string_view(lhs) == rhs.internal_data(); }
inline bool operator!=(const char* lhs, const utf8_string& rhs) { return std::string_view(lhs) != rhs.internal_data(); }
inline bool operator<(const char* lhs, const utf8_string& rhs) { return std::string_view(lhs) < rhs.internal_data(); }

inline std::ostream& operator<<(std::ostream& os, const utf8_string& str) {
    return os << str.internal_data();
}

inline std::istream& operator>>(std::istream& is, utf8_string& str) {
    std::string temp;
    if (is >> temp) {
        str = temp;
    }
    return is;
}

} // namespace kvasir

namespace std {

template <>
struct hash<kvasir::utf8_string> {
    using is_transparent = void;

    size_t operator()(const kvasir::utf8_string& str) const {
        return kvasir::detail::fnv1a_hash(str.internal_data().data(), str.internal_data().size());
    }

    size_t operator()(std::string_view str) const {
        return kvasir::detail::fnv1a_hash(str.data(), str.size());
    }
};

} // namespace std