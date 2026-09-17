#ifndef MATRIX_TESTS_H
#define MATRIX_TESTS_H

#include <cstddef>
namespace matrix::tests {

int run_basic();
int run_perf_basic(std::size_t pair_count);
int run_perf_large_values(std::size_t pair_count, std::size_t value_size);
int run_perf_overwrite(std::size_t pair_count, std::size_t overwrite_times);
int run_hash_collision(std::size_t pair_count, std::size_t bucket_count);

} // namespace matrix::tests

#endif // MATRIX_TESTS_H
