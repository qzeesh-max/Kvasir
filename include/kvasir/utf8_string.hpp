#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include "hash.hpp"
#if defined(__ARM_NEON)
#include <arm_neon.h>
#elif defined(__AVX2__)
#include <immintrin.h>
#endif

#include <string_view>
#include <functional>
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
        size_t cp_len = 0;
        data_.erase(data_.size() - 3); // Remove padding temporarily
        if (cp <= 0x7F) {
            data_.push_back(static_cast<char>(cp)); cp_len = 1;
        } else if (cp <= 0x7FF) {
            data_.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
            data_.push_back(static_cast<char>(0x80 | (cp & 0x3F))); cp_len = 2;
        } else if (cp <= 0xFFFF) {
            data_.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
            data_.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            data_.push_back(static_cast<char>(0x80 | (cp & 0x3F))); cp_len = 3;
        } else if (cp <= 0x10FFFF) {
            data_.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
            data_.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            data_.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            data_.push_back(static_cast<char>(0x80 | (cp & 0x3F))); cp_len = 4;
        }
        data_.append(3, '\0'); // Re-add padding
        
        size_t prev_byte_idx = logical_size() - cp_len;
        size_t local_idx = code_point_count_ % kCodePointsPerFragment;
        
        if (local_idx == 0) {
            CodePointIndexFragment frag;
            frag.fragmentFirstCharIndex = prev_byte_idx;
            std::memset(frag.fragmentCharIndexDiffs, 0, kFragmentDiffsSize);
            frag.fragmentCharIndexDiffs[0] = static_cast<uint8_t>(logical_size() - prev_byte_idx);
            add_fragment(frag, code_point_count_ / kCodePointsPerFragment);
        } else {
            auto& frag = get_mutable_fragment(code_point_count_ / kCodePointsPerFragment);
            frag.fragmentCharIndexDiffs[local_idx] = static_cast<uint8_t>(logical_size() - frag.fragmentFirstCharIndex);
        }
        code_point_count_++;
    }

    utf8_string& append(const char* str) {
        size_t old_logical = logical_size();
        size_t old_cp = code_point_count_;
        data_.erase(data_.size() - 3);
        data_.append(str);
        data_.append(3, '\0');
        append_index(old_logical, old_cp);
        return *this;
    }

    utf8_string& append(const std::string& str) {
        size_t old_logical = logical_size();
        size_t old_cp = code_point_count_;
        data_.erase(data_.size() - 3);
        data_.append(str);
        data_.append(3, '\0');
        append_index(old_logical, old_cp);
        return *this;
    }
    
    utf8_string& append(const utf8_string& str) {
        size_t old_logical = logical_size();
        size_t old_cp = code_point_count_;
        data_.erase(data_.size() - 3);
        data_.append(str.internal_data());
        data_.append(3, '\0');
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
        data_.insert(byte_pos, str.internal_data());
        rebuild_index();
        return *this;
    }
    
    // Erase
    utf8_string& erase(size_t pos = 0, size_t count = std::string::npos) {
        if (pos >= code_point_count_) return *this;
        size_t byte_pos = byte_index(pos);
        size_t byte_end;
        if (count == std::string::npos || pos + count >= code_point_count_) {
            byte_end = logical_size();
        } else {
            byte_end = byte_index(pos + count);
        }
        data_.erase(byte_pos, byte_end - byte_pos);
        rebuild_index();
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
        rebuild_index();
        return *this;
    }
    
    // Batch decoding using SIMD for fast paths
    size_t decode_code_points(size_t cp_pos, size_t count, uint32_t* out) const {
        if (cp_pos >= code_point_count_) return 0;
        if (cp_pos + count > code_point_count_) count = code_point_count_ - cp_pos;
        
        size_t byte_idx = byte_index(cp_pos);
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data_.data());
        size_t cp_decoded = 0;
        
        while (cp_decoded < count) {
#if defined(__ARM_NEON)
            if (cp_decoded + 16 <= count && byte_idx + 16 <= logical_size()) {
                uint8x16_t chunk = vld1q_u8(p + byte_idx);
                uint8x16_t top_bits = vandq_u8(chunk, vdupq_n_u8(0x80));
                if (vmaxvq_u8(top_bits) == 0) {
                    uint16x8_t low_16 = vmovl_u8(vget_low_u8(chunk));
                    uint16x8_t high_16 = vmovl_u8(vget_high_u8(chunk));
                    uint32x4_t low_32_1 = vmovl_u16(vget_low_u16(low_16));
                    uint32x4_t low_32_2 = vmovl_u16(vget_high_u16(low_16));
                    uint32x4_t high_32_1 = vmovl_u16(vget_low_u16(high_16));
                    uint32x4_t high_32_2 = vmovl_u16(vget_high_u16(high_16));
                    
                    vst1q_u32(out + cp_decoded, low_32_1);
                    vst1q_u32(out + cp_decoded + 4, low_32_2);
                    vst1q_u32(out + cp_decoded + 8, high_32_1);
                    vst1q_u32(out + cp_decoded + 12, high_32_2);
                    
                    byte_idx += 16;
                    cp_decoded += 16;
                    continue;
                }
            }
#elif defined(__AVX2__)
            if (cp_decoded + 16 <= count && byte_idx + 16 <= logical_size()) {
                __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + byte_idx));
                if (_mm_movemask_epi8(chunk) == 0) {
                    __m256i low_32 = _mm256_cvtepu8_epi32(chunk);
                    __m256i high_32 = _mm256_cvtepu8_epi32(_mm_srli_si128(chunk, 8));
                    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + cp_decoded), low_32);
                    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + cp_decoded + 8), high_32);
                    byte_idx += 16;
                    cp_decoded += 16;
                    continue;
                }
            }
#endif
            out[cp_decoded] = decode_code_point(byte_idx);
            byte_idx += utf8_len_table[p[byte_idx]];
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
    
    void append_index(size_t byte_idx, size_t cp_count) {
        size_t data_size = logical_size();
        if (byte_idx >= data_size) return;
        
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data_.data());
        
        size_t local_idx = cp_count % kCodePointsPerFragment;
        size_t frag_idx = cp_count / kCodePointsPerFragment;
        
        CodePointIndexFragment frag;
        if (local_idx > 0) {
            frag = get_fragment(frag_idx);
        } else {
            frag.fragmentFirstCharIndex = byte_idx;
            std::memset(frag.fragmentCharIndexDiffs, 0, kFragmentDiffsSize);
        }

        while (byte_idx < data_size) {
            if (local_idx == kCodePointsPerFragment) {
                add_fragment(frag, frag_idx++, (data_size / kCodePointsPerFragment) + 1);
                frag.fragmentFirstCharIndex = byte_idx;
                std::memset(frag.fragmentCharIndexDiffs, 0, kFragmentDiffsSize);
                local_idx = 0;
            }
            
            // ASCII fast path: process 8 bytes at a time
            if (local_idx + 8 <= kCodePointsPerFragment && byte_idx + 8 <= data_size) {
                uint64_t chunk;
                std::memcpy(&chunk, p + byte_idx, 8);
                if ((chunk & 0x8080808080808080ULL) == 0) {
                    size_t start_diff = byte_idx - frag.fragmentFirstCharIndex;
                    for (size_t i = 0; i < 8; ++i) {
                        frag.fragmentCharIndexDiffs[local_idx + i] = static_cast<uint8_t>(start_diff + i + 1);
                    }
                    byte_idx += 8;
                    local_idx += 8;
                    cp_count += 8;
                    continue;
                }
            }
            
            size_t cp_len = utf8_len_table[p[byte_idx]];
            size_t remaining = data_size - byte_idx;
            byte_idx += (cp_len < remaining) ? cp_len : remaining;
            
            frag.fragmentCharIndexDiffs[local_idx] = static_cast<uint8_t>(byte_idx - frag.fragmentFirstCharIndex);
            
            local_idx++;
            cp_count++;
        }
        code_point_count_ = cp_count;
        if (local_idx > 0 || cp_count == 0) {
            add_fragment(frag, frag_idx, (data_size / kCodePointsPerFragment) + 1);
        }
    }
    void rebuild_index() {
        heap_index_.clear();
        code_point_count_ = 0;
        size_t byte_idx = 0;
        size_t data_size = logical_size();
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data_.data());
        

        
        CodePointIndexFragment frag;
        frag.fragmentFirstCharIndex = 0;
        std::memset(frag.fragmentCharIndexDiffs, 0, kFragmentDiffsSize);
        size_t local_idx = 0;
        size_t frag_idx = 0;
        size_t cp_count = 0;

        while (byte_idx < data_size) {
            if (local_idx == kCodePointsPerFragment) {
                add_fragment(frag, frag_idx++, (data_size / kCodePointsPerFragment) + 1);
                frag.fragmentFirstCharIndex = byte_idx;
                std::memset(frag.fragmentCharIndexDiffs, 0, kFragmentDiffsSize);
                local_idx = 0;
            }
            
            // ASCII fast path: process 8 bytes at a time
            if (local_idx + 8 <= kCodePointsPerFragment && byte_idx + 8 <= data_size) {
                uint64_t chunk;
                std::memcpy(&chunk, p + byte_idx, 8);
                if ((chunk & 0x8080808080808080ULL) == 0) {
                    size_t start_diff = byte_idx - frag.fragmentFirstCharIndex;
                    for (size_t i = 0; i < 8; ++i) {
                        frag.fragmentCharIndexDiffs[local_idx + i] = static_cast<uint8_t>(start_diff + i + 1);
                    }
                    byte_idx += 8;
                    local_idx += 8;
                    cp_count += 8;
                    continue;
                }
            }
            
            size_t cp_len = utf8_len_table[p[byte_idx]];
            size_t remaining = data_size - byte_idx;
            byte_idx += (cp_len < remaining) ? cp_len : remaining;
            
            frag.fragmentCharIndexDiffs[local_idx] = static_cast<uint8_t>(byte_idx - frag.fragmentFirstCharIndex);
            
            local_idx++;
            cp_count++;
        }
        code_point_count_ = cp_count;
        if (local_idx > 0 || cp_count == 0) {
            add_fragment(frag, frag_idx, (data_size / kCodePointsPerFragment) + 1);
        }
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
        
        while (i < size) {
#if defined(__ARM_NEON)
            if (i + 16 <= size) {
                uint8x16_t chunk = vld1q_u8(p + i);
                uint8x16_t top_bits = vandq_u8(chunk, vdupq_n_u8(0x80));
                if (vmaxvq_u8(top_bits) == 0) {
                    i += 16;
                    continue;
                }
            }
#elif defined(__AVX2__)
            if (i + 16 <= size) {
                __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + i));
                if (_mm_movemask_epi8(chunk) == 0) {
                    i += 16;
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