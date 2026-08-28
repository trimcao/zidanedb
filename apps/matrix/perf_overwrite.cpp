#include "matrix.h"
#include "zidanedb/database.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>

namespace matrix::tests {

int run_perf_overwrite(std::size_t pair_count, std::size_t overwrite_times) {
    using Clock = std::chrono::steady_clock;
    using Seconds = std::chrono::duration<double>;

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "matrix-overwrite-performance.zdb";

    const std::filesystem::path idx_path =
        std::filesystem::temp_directory_path() / "matrix-overwrite-performance.zidx";

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

        // Initial insertion
        for (const auto& [key, value] : expected) {
            database.put(key, value);
        }

        // Actual overwrites
        for (std::size_t iteration = 0; iteration < overwrite_times; ++iteration) {
            for (const auto& [key, value] : expected) {
                database.put(key, value);
            }
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

    const auto database_size = std::filesystem::file_size(path);
    const auto index_size = std::filesystem::file_size(idx_path);
    const auto total_puts = pair_count * (overwrite_times + 1);

    std::cout << "Pairs:             " << pair_count << '\n';
    std::cout << "Overwrite per key: " << overwrite_times << '\n';
    std::cout << "Put time:          " << put_duration.count() << " seconds\n";
    std::cout << "Total PUTs:        " << total_puts << '\n';
    std::cout << "Put rate:          " << total_puts / put_duration.count() << " ops/second\n";
    std::cout << "Load time:         " << load_duration.count() << " seconds\n";
    std::cout << "Verify time:       " << verify_duration.count() << " seconds\n";
    std::cout << "Get rate:          " << pair_count / verify_duration.count() << " ops/second\n";
    std::cout << "Database size:     " << database_size << " bytes\n";
    std::cout << "Index size:        " << index_size << " bytes\n";

    std::filesystem::remove(path);
    std::filesystem::remove(idx_path);

    return 0;
}

} // namespace matrix::tests
