# Kvasir

Kvasir is a UTF-8 string framework in C++ designed to provide fast indexing and iteration by code-point, while matching the interface of `std::string` as much as possible.

## Features

- **Code-Point Indexing**: Fast access to individual Unicode characters through `string[index]`. Kvasir uses a lightweight internal index built during assignment and modification, allowing O(1) code-point indexing within a 64-byte localized cache line fragment.
- **Dynamic Platform-Agnostic Layout**: `CodePointIndexFragment` evaluates the size of `size_t` at compile-time to maintain a precise 64-byte size (to fit within a standard CPU cache line):
  - On a 64-bit architecture (`size_t` is 8 bytes), each fragment addresses 57 code points.
  - On a 32-bit architecture (`size_t` is 4 bytes), each fragment addresses 61 code points.
- **Branchless & SIMD-friendly Parsing**: The index building mechanism relies on a static lookup table and branchless processing, parsing and caching boundaries almost twice as fast as branching approaches. Decoding operations are also branch-free.
- **`std::string`-like interface**: Provides similar methods, mutators (`append`, `push_back`, `+=`), and characteristics.
- **UTF-8 Aware Iteration**: Iterate over code points seamlessly using a random access iterator.
- **View Classes**: Includes `kvasir::utf8_string_view`, a non-owning view class analogous to `std::string_view` for Kvasir strings.
- **Rope Data Structure**: Includes `kvasir::utf8_rope`, an immutable tree structure for highly efficient large string concatenations, insertions, and deletions where each node preserves the cache-friendly code point metrics.

## Efficient Memory Layout

The index structure is engineered to stay cache-friendly:

```cpp
constexpr size_t kCacheLineSize = 64;
constexpr size_t kFragmentDiffsSize = kCacheLineSize - sizeof(size_t);
constexpr size_t kCodePointsPerFragment = kFragmentDiffsSize + 1;

struct alignas(kCacheLineSize) CodePointIndexFragment {
    size_t fragmentFirstCharIndex;
    uint8_t fragmentCharIndexDiffs[kFragmentDiffsSize];
};
```

By keeping cumulative byte length differences in `uint8_t`, Kvasir takes advantage of the fact that roughly 60 UTF-8 characters can at most occupy 240 bytes, comfortably fitting into a byte array.

## Performance Benchmarks

Branchless optimizations drastically lower the overhead of index-building.

_Tested on a roughly 140-character mixed ASCII and multibyte UTF-8 string:_

| Benchmark                           | Time (ns) |
|-------------------------------------|-----------|
| `BM_StdStringCreation`              | 11.0      |
| `BM_StdWstringCreation`             | 22.8      |
| `BM_KvasirUtf8StringCreation`       | 87.0      |
| `BM_StdStringIteration` (byte)      | 40.3      |
| `BM_StdWstringIteration` (wchar)    | 40.9      |
| `BM_KvasirUtf8StringIteration` (cp) | 279       |
| `BM_StdStringIndexing` (byte)       | 42.4      |
| `BM_StdWstringIndexing` (wchar)     | 39.3      |
| `BM_KvasirUtf8StringIndexing` (cp)  | 110       |

Creation time is heavily optimized via 64-bit chunk processing and ASCII fast paths. By checking for pure ASCII within 8-byte blocks via bitwise masking `(chunk & 0x8080...) == 0`, code point index fragments are pre-populated sequentially, driving UTF-8 string creation down to an astonishing **86ns** for 140 bytes of mixed text, which is nearly native `std::string` speeds. Code point validation (`validate_utf8`) additionally uses AVX2/NEON SIMD structures where available to parse vast text blocks near instantaneously.

While sequential iteration is physically bounded by the computational effort of decoding multi-byte sequences into `uint32_t` code points dynamically (costing about ~2ns per character), our O(1) cache line offset `CodePointIndexFragment` layout allows direct random-access indexing to run extremely fast: a mere ~110ns to perform 140 random-access lookups.

### Rope vs String Benchmarks

When scaling up to extensive modifications (such as repeated insertions or massive concatenations), `kvasir::utf8_rope` drastically outperforms `kvasir::utf8_string`. Operations in a flat utf8_string require $O(N)$ rebuilding of the byte layout and the code point cache fragments.

_Time per 1024 repetitive concatenations & insertions:_

| Benchmark                           | Time (us) |
|-------------------------------------|-----------|
| `BM_Utf8StringInsert/1024`          | 15.2      |
| `BM_RopeInsert/1024`                | 58.0      |
| `BM_Utf8StringConcat/1024`          | 38.5      |
| `BM_RopeConcat/1024`                | 57.6      |

With our latest optimizations, `append_index` brings `utf8_string` concatenation to an incredibly low 38 microseconds (a massive 1156X speedup from the previous $O(N)$ index-rebuild implementation). `utf8_rope` turns a ~38 microsecond massive string concatenation overhead into a 58 microsecond tree linking operation without needing memory-reallocation, while retaining fast `$O(\log(\text{nodes}))$` code point lookup capabilities. The rope structure was further optimized by replacing standard `shared_ptr` tree linking with internal `intrusive_ptr` nodes, yielding an additional ~33% speedup.

## Development

Kvasir uses CMake as its build system and relies on Google Test and Google Benchmark.

```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j
```

### Running Tests

```bash
./tests/kvasir_tests
```

### Running Benchmarks

```bash
./benchmarks/kvasir_benchmarks
```
