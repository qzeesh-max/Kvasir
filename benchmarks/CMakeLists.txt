add_executable(kvasir_benchmarks
    benchmark_utf8_string.cpp
    benchmark_utf8_rope.cpp
    benchmark_utf8_complex.cpp
    benchmark_cow_slice.cpp
    benchmark_perf_deep.cpp
)
target_link_libraries(kvasir_benchmarks PRIVATE kvasir benchmark::benchmark benchmark::benchmark_main)
