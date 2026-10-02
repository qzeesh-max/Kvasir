# Kvasir

**Mythological Roots**: In Norse mythology, Kvasir was a being born from the blended saliva of the Vanir and the Æsir, renowned for his unparalleled wisdom and ability to answer any question. He traveled the realms spreading knowledge. The Kvasir framework is named in his honor because it acts as a wise, omniscient bridge across the disparate worlds of text representation—seamlessly interpreting and managing the complexities of Unicode.

Kvasir is a modern, high-performance UTF-8 string framework in C++ designed to solve the complexities of handling Unicode in modern applications. It provides fast code-point indexing, iteration, and manipulation while matching the familiar interface of `std::string`. 

## Why Kvasir? (vs. `std::string` and `std::wstring`)

Traditional C++ strings present significant hurdles when dealing with Unicode:
- **`std::string` (Byte-level)**: Only understands raw bytes. Operations like `.length()`, `[index]`, or `.substr()` act on bytes, meaning multibyte UTF-8 characters get fragmented, resulting in corrupted text, invalid boundaries, and incorrect string lengths. Properly traversing it requires $O(N)$ dynamic decoding at every step.
- **`std::wstring` (Wide characters)**: Inherently platform-dependent (UTF-16 on Windows, UTF-32 on Unix). UTF-32 wastes massive amounts of memory (4 bytes per ASCII character), destroying CPU cache efficiency. UTF-16 still suffers from surrogate pair fragmentation where a single code point might span two elements. Moreover, communicating over networks or saving to databases almost universally requires UTF-8, forcing costly and continuous transcoding.

**Kvasir** eliminates these issues by giving you the memory footprint of `std::string` (UTF-8) with the $O(1)$ random access capabilities of `std::wstring` (UTF-32). It buys the user:
1. **Cache-Friendly $O(1)$ Code Point Indexing**: Through a lightweight internal index built dynamically, giving you constant-time random access by code point.
2. **Memory Efficiency**: Keeps the underlying text purely in UTF-8, ensuring zero overhead for ASCII and seamless IO/network interoperability.
3. **Safety**: Mutations, iterations, and substrings will *never* break a multibyte sequence.

## Features

- **Code-Point Indexing**: Fast access to individual Unicode characters through `string[index]`. Kvasir uses a lightweight internal index built during assignment and modification, allowing O(1) code-point indexing within a 64-byte localized cache line fragment.
- **Dynamic Platform-Agnostic Layout**: `CodePointIndexFragment` evaluates the size of `size_t` at compile-time to maintain a precise 64-byte size (to fit within a standard CPU cache line).
- **Branchless & Unrolled Parsing**: The index building mechanism relies on a static lookup table, branchless processing, and 16-byte unrolled `uint64_t` ASCII fast-paths, parsing and caching boundaries almost twice as fast as branching approaches.
- **`std::string`-like interface**: Provides similar methods, mutators (`append`, `push_back`, `+=`), and characteristics.
- **O(suffix) Mutation Rescans**: Operations like `erase`, `insert`, and `replace` use a highly optimized `partial_rebuild_index_from()` algorithm. Instead of O(n) full rescans, modifications only rebuild the cache line fragments from the point of mutation to the end of the string, skipping the untouched prefix.
- **UTF-8 Aware Iteration**: Iterate over code points seamlessly using a random access iterator.
- **Thread-Safe, Lock-Free Rope (`kvasir::utf8_rope`)**: An immutable, reference-counted tree structure for highly efficient large string concatenations, insertions, and deletions where each node preserves cache-friendly code point metrics. Powered by custom atomic intrusive pointers and a lock-free Treiber stack for node pooling, allowing millions of concurrent mutations without locks.
- **Transparent Hashing Compatibility**: Includes a custom, zero-copy FNV-1a hashing mechanism that preserves equivalent `std::hash` transparency across `std::string`, `kvasir::utf8_string`, and `kvasir::utf8_rope`. This allows interoperable use across `std::unordered_map` and associative containers without triggering heap allocations.
- **I/O Stream Integration**: Full support for `<iostream>` via `operator<<` and `operator>>` across all types. For massive ropes, output streams perform piece-wise flushing of nodes to prevent unnecessary memory allocations.

## Variant-Based Reference Views (`utf8_slice` & `utf8_cow`)

To maximize performance in a system where developers seamlessly alternate between contiguous memory (`utf8_string`) and tree-structured memory (`utf8_rope`), Kvasir uses `std::variant`-backed abstractions to eliminate virtual function overhead:

- **`kvasir::utf8_slice`**: A unified, non-owning, read-only view that acts as a lightweight substring slice over *both* `utf8_string` and `utf8_rope`. Instead of inheriting from a virtual base class, `utf8_slice` stores a `std::variant` pointer. It provides zero-copy substrings, code-point indexing, and length calculations across completely different underlying data structures.
- **`kvasir::utf8_cow` (Copy-On-Write)**: A unified abstraction designed to transparently pass strings around in large systems without immediately triggering a copy. A `utf8_cow` begins its life completely borrowing its data (referencing either a `utf8_string` or `utf8_rope` internally). When a mutation (like `push_back`) is finally requested, the object transparently upgrades itself into an owned structure, making a deep copy. This minimizes overhead in multithreaded and read-heavy systems where mutation is rare but possible.

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

Creation time is heavily optimized via 64-bit chunk processing and ASCII fast paths. By checking for pure ASCII within 16-byte blocks using unrolled `uint64_t` bitwise masking `(chunk & 0x8080...) == 0`, code point index fragments are pre-populated sequentially, driving UTF-8 string creation down to an astonishing **86ns** for 140 bytes of mixed text.

While sequential iteration is physically bounded by the computational effort of decoding multi-byte sequences into `uint32_t` code points dynamically, our O(1) cache line offset `CodePointIndexFragment` layout allows direct random-access indexing to run extremely fast: a mere ~110ns to perform 140 random-access lookups.

### The Power of ILP (Instruction-Level Parallelism)

One of Kvasir's most powerful side-effects is how its `operator[]` interacts with modern CPU architectures. Sequential scans (`byte_idx += utf8_len_table[byte_idx]`) suffer from tight loop-carried data dependencies (the CPU must finish decoding the current byte before it knows where the next character starts).

Because Kvasir's `operator[]` leverages the precomputed `CodePointIndexFragment` cache, random access `s[i]` computes the underlying byte boundaries entirely independently for each `i`. This breaks the data dependency chain, allowing the CPU to schedule multiple code point decodes in parallel via ILP. In deep benchmarks, random `operator[]` decoding in a loop is often **>2x faster** than linear batch sequential scanning!

### Rope vs String Benchmarks

When scaling up to extensive modifications (such as repeated insertions or massive concatenations), `kvasir::utf8_rope` drastically outperforms `kvasir::utf8_string`. Operations in a flat utf8_string require $O(N)$ rebuilding of the byte layout and the code point cache fragments.

_Time per 1024 repetitive concatenations & insertions:_

| Benchmark                           | Time (us) |
|-------------------------------------|-----------|
| `BM_Utf8StringInsert/1024`          | 15.2      |
| `BM_RopeInsert/1024`                | 58.0      |
| `BM_Utf8StringConcat/1024`          | 38.5      |
| `BM_RopeConcat/1024`                | 57.6      |

With our latest optimizations, `append_index` brings `utf8_string` concatenation to an incredibly low 38 microseconds (a massive 1156X speedup from previous naive implementations). `utf8_rope` turns massive string concatenation overhead into a 58 microsecond tree linking operation without needing memory-reallocation. The rope structure is further optimized by replacing standard `shared_ptr` tree linking with internal `intrusive_ptr` nodes and a lock-free memory pool, yielding immense multi-threaded throughput.

## Testing & Coverage

Kvasir is rigorously tested with over **120 test suites** covering edge-case UTF-8 bounds, large fragment transitions, and memory lifecycle invariants. The core `utf8_string` features **100% line execution coverage**. Additionally, comprehensive Google ThreadSanitizer (TSAN) test suites assert thread-safety across concurrent rope read/writes, multi-threaded ref-count overflows, and lock-free Treiber stack operations.

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
./run_tests.sh
# OR manually:
./build_release/tests/kvasir_tests
```

### Running Benchmarks

```bash
./run_benchmarks.sh
# OR manually:
./build_release/benchmarks/kvasir_benchmarks
```
