#ifndef MATRIX_TESTS_H
#define MATRIX_TESTS_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
namespace matrix::tests {

// note: will use WorkloadOptions later
struct WorkloadOptions {
    std::size_t pair_count{10'000};
    std::uint64_t seed{0x51DA7E};
    std::filesystem::path database_path;
};

int run_basic();
int run_perf_basic(std::size_t pair_count);

} // namespace matrix::tests

#endif // MATRIX_TESTS_H