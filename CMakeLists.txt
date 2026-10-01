cmake_minimum_required(VERSION 3.14)
project(Kvasir CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Fetch GoogleTest
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Fetch Google Benchmark
FetchContent_Declare(
  googlebenchmark
  URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.zip
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

add_library(kvasir INTERFACE)
target_include_directories(kvasir INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

enable_testing()

add_subdirectory(tests)
add_subdirectory(benchmarks)

# Install targets
include(GNUInstallDirs)

install(DIRECTORY include/kvasir
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(TARGETS kvasir_tests
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    OPTIONAL
)

install(TARGETS kvasir_benchmarks
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    OPTIONAL
)

# Export configuration for find_package
install(TARGETS kvasir EXPORT KvasirTargets)

install(EXPORT KvasirTargets
    FILE KvasirTargets.cmake
    NAMESPACE Kvasir::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Kvasir
)

include(CMakePackageConfigHelpers)
write_basic_package_version_file("KvasirConfigVersion.cmake"
    VERSION 1.0.0
    COMPATIBILITY SameMajorVersion
)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/KvasirConfig.cmake"
    "include(\"\${CMAKE_CURRENT_LIST_DIR}/KvasirTargets.cmake\")\n"
)

install(FILES 
    "${CMAKE_CURRENT_BINARY_DIR}/KvasirConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/KvasirConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Kvasir
)
