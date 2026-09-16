#include "matrix.h"
#include "utils.h"
#include "zidanedb/database.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace matrix::tests {

int run_perf_large_values(std::size_t pair_count, std::size_t value_size) {
    using Clock = std::chrono::steady_clock;
    using Seconds = std::chrono::duration<double>;

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "matrix-large-values.zdb";

    const std::filesystem::path idx_path =
        std::filesystem::temp_directory_path() / "matrix-large-values.zdb.idx";

    std::cout << "Database file: " << path << '\n';

    std::filesystem::remove(path);
    std::filesystem::remove(idx_path);

    // Generate data before timing database operations
    std::vector<std::pair<std::string, std::string>> records;
    records.reserve(pair_count);
    std::mt19937_64 random_engine{0x51DA7E};

    {
        for (std::size_t index = 0; index < pair_count; ++index) {
            std::string key = "large-key-" + std::to_string(index);
            std::string value = detail::random_string(random_engine, value_size);
            records.emplace_back(std::move(key), std::move(value));
        }
    }

    Seconds put_duration;

    {
        zidanedb::Database database{path};

        const auto start = Clock::now();

        for (const auto& [key, value] : records) {
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

    for (const auto& [key, expected_value] : records) {
        const auto actual_value = reopened.get(key);

        if (!actual_value) {
            std::cerr << "Missing key: " << key << '\n';
            return 1;
        }
        if (*actual_value != expected_value) {
            std::cerr << "Incorrect value for: " << key << '\n';
            return 1;
        }
    }

    const auto verify_end = Clock::now();
    const Seconds verify_duration = verify_end - verify_start;

    const auto database_size = std::filesystem::file_size(path);
    const auto index_size = std::filesystem::file_size(idx_path);

    const double total_bytes = static_cast<double>(pair_count) * static_cast<double>(value_size);
    const double total_mib = total_bytes / (1024.0 * 1024.0);

    std::cout << "Pairs:          " << pair_count << '\n';
    std::cout << "Value size:     " << value_size << " bytes\n";
    std::cout << "Value data:     " << total_mib << " MiB\n";
    std::cout << "Put time:       " << put_duration.count() << " seconds\n";
    std::cout << "Put rate:       " << pair_count / put_duration.count() << " ops/second\n";
    std::cout << "Put throughput: " << total_mib / put_duration.count() << " MiB/second\n";
    std::cout << "Load time:      " << load_duration.count() << " seconds\n";
    std::cout << "Verify time:    " << verify_duration.count() << " seconds\n";
    std::cout << "Get rate:       " << pair_count / verify_duration.count() << " ops/second\n";
    std::cout << "Get throughput: " << total_mib / verify_duration.count() << " MiB/second\n";
    std::cout << "Database size:  " << database_size << " bytes\n";
    std::cout << "Index size:     " << index_size << " bytes\n";

    std::filesystem::remove(path);
    std::filesystem::remove(idx_path);

    return 0;
}

} // namespace matrix::tests
