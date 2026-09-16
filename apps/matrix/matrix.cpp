#include "matrix.h"
#include <CLI/CLI.hpp>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    CLI::App app{"Matrix: Tormentor of ZidaneDB"};
    app.set_version_flag("--version", "Matrix 0.1.0");

    auto* basic = app.add_subcommand("basic", "Verify persistence across reopening");

    std::size_t pair_count = 10'000;
    auto* perf_basic = app.add_subcommand("perf-basic", "Measure basic persistence performance");
    perf_basic->add_option("-n,--pairs", pair_count, "Number of pairs");

    std::size_t large_pair_count = 1'000;
    std::size_t large_value_size = 64 * 1024;
    auto* perf_large_values = app.add_subcommand("perf-large-values", "Workload with large values");
    perf_large_values->add_option("-n,--pairs", large_pair_count, "Number of pairs");
    perf_large_values->add_option("-s,--value-size", large_value_size, "Value size in bytes");

    std::size_t overwrite_pair_count = 1'000;
    std::size_t overwrite_times = 100;
    auto* perf_overwrite =
        app.add_subcommand("perf-overwrite", "Workload with a lot of overwrites");
    perf_overwrite->add_option("-n,--pairs", overwrite_pair_count, "Number of pairs")
        ->check(CLI::PositiveNumber);
    perf_overwrite->add_option("-o,--overwrites", overwrite_times, "Overwrite times of each key")
        ->check(CLI::PositiveNumber);

    std::size_t hash_collision_pair_count = 1'000'000;
    std::size_t hash_collision_bucket_count = 1'000'000;
    auto* hash_collision = app.add_subcommand(
        "hash-collision", "Benchmark for hash collisions in a persistent hash index");
    hash_collision->add_option("-n,--pairs", hash_collision_pair_count, "Number of pairs")
        ->check(CLI::PositiveNumber);
    hash_collision->add_option("-b,--buckets", hash_collision_bucket_count, "Number of buckets")
        ->check(CLI::PositiveNumber);

    app.require_subcommand(1, 1);

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        return app.exit(error);
    }

    try {
        if (*basic) {
            return matrix::tests::run_basic();
        }

        if (*perf_basic) {
            return matrix::tests::run_perf_basic(pair_count);
        }

        if (*perf_large_values) {
            return matrix::tests::run_perf_large_values(large_pair_count, large_value_size);
        }

        if (*perf_overwrite) {
            return matrix::tests::run_perf_overwrite(overwrite_pair_count, overwrite_times);
        }

        if (*hash_collision) {
            return matrix::tests::run_hash_collision(hash_collision_pair_count,
                                                     hash_collision_bucket_count);
        }
    } catch (const std::exception& error) {
        std::cerr << "matrix: " << error.what() << '\n';
        return 1;
    }

    return 1; // Defensive:: successful parsing should select a test.
}
