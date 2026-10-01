#pragma once

#include <cstddef>
#include <string_view>

namespace kvasir {
namespace detail {

constexpr size_t fnv1a_offset_basis = (sizeof(size_t) == 8) ? 14695981039346656037ULL : 2166136261U;
constexpr size_t fnv1a_prime = (sizeof(size_t) == 8) ? 1099511628211ULL : 16777619U;

inline size_t fnv1a_append(size_t hash, const char* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<size_t>(static_cast<unsigned char>(data[i]));
        hash *= fnv1a_prime;
    }
    return hash;
}

inline size_t fnv1a_hash(const char* data, size_t len) {
    return fnv1a_append(fnv1a_offset_basis, data, len);
}

} // namespace detail
} // namespace kvasir
