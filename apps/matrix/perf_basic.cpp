#include "matrix.h"
#include "zidanedb/database.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>

namespace matrix::tests {

int run_perf_basic(std::size_t pair_count) {
    using Clock = std::chrono::steady_clock;
    using Seconds = std::chrono::duration<double>;

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "matrix-performance.zdb";

    const std::filesystem::path idx_path =
        std::filesystem::temp_directory_path() / "matrix-performance.zidx";

    std::cout << "Database file: " << path << '\n';

    std::filesystem::remove(path);
    std::filesystem::remove(idx_path);

    std::unordered_map<std::string, std::string> expected;
    expected.reserve(pair_count);

    // Generate data before timing database operations
    {
        for (std::size_t index = 0; index < pair_count; ++index) {
            expected.emplace("key-" + std::to_string(index), "value-" + std::to_string(index));
        }
    }

    Seconds put_duration;

    {
        zidanedb::Database database{path};

        const auto start = Clock::now();

        for (const auto& [key, value] : expected) {
            database.put(key, value);
        }

        const auto end = Clock::now();
        put_duration = end - start;
    } // Destroy the first database

    const auto load_start = Clock::now();
    const zidanedb::Database reopened{path};
    const auto load_end = Clock::now();

    const Seconds load_duration = load_end - load_start;

    const auto verify_start = Clock::now();

    for (const auto& [key, expected_value] : expected) {
        const auto actual_value = reopened.get(key);

        if (!actual_value || *actual_value != expected_value) {
            std::cerr << "Verification failed for " << key << '\n';
            return 1;
        }
    }

    const auto verify_end = Clock::now();
    const Seconds verify_duration = verify_end - verify_start;

    const auto file_size = std::filesystem::file_size(path);

    std::cout << "Pairs:        " << pair_count << '\n';
    std::cout << "Put time:     " << put_duration.count() << " seconds\n";
    std::cout << "Put rate:     " << pair_count / put_duration.count() << " ops/second\n";
    std::cout << "Load time:    " << load_duration.count() << " seconds\n";
    std::cout << "Verify time:  " << verify_duration.count() << " seconds\n";
    std::cout << "Get rate:     " << pair_count / verify_duration.count() << " ops/second\n";
    std::cout << "File size:    " << file_size << " bytes\n";

    std::filesystem::remove(path);
    std::filesystem::remove(idx_path);

    return 0;
}

} // namespace matrix::tests
