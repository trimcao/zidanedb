#include "matrix.h"
#include "utils.h"
#include "zidanedb/database.h"
#include <filesystem>
#include <iostream>
#include <random>
#include <chrono>

namespace matrix::tests {

int run_basic() {
    constexpr std::size_t pair_count = 10'000;

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        "zidanedb-matrix-test.zdb";

    std::filesystem::remove(path);

    // A fixed seed makes failure reproducible
    std::mt19937_64 random_engine{0x51DA7E};

    std::unordered_map<std::string, std::string> expected;

    {
        zidanedb::Database database{path};
        for (std::size_t index = 0; index < pair_count; ++index) {
            // The index guarantees that every key is unique
            std::string key =
                "key-" +
                std::to_string(index) +
                "-" +
                matrix::tests::detail::random_string(random_engine, 12);

            std::string value =
                matrix::tests::detail::random_string(random_engine, 32);

            expected.emplace(key, value);
            database.put(key, value);

            if ((index + 1) % 1'000 == 0 ) {
                std::cout
                    << "Inserted "
                    << index + 1
                    << " pairs\n";
            }
        }
    } // Destroy the writing database

    {
        const zidanedb::Database database{path};

        for (const auto& [key, expected_value] : expected) {
            const auto actual_value = database.get(key);

            if (!actual_value.has_value()) {
                std::cerr << "Missing key: " << key << '\n';
                return 1;
            }
            if (*actual_value != expected_value) {
                std::cerr << "Incorrect value for: " << key << '\n';
                return 1;
            }
        }
    } // Destroy the reopened database

    std::filesystem::remove(path);

    std::cout
        << "PASS: verified "
        << expected.size()
        << " persisted pairs\n";

    return 0;
}

int run_perf_basic(std::size_t pair_count) {
    using Clock = std::chrono::steady_clock;
    using Seconds = std::chrono::duration<double>;

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        "matrix-performance.zdb";

    const std::filesystem::path idx_path =
        std::filesystem::temp_directory_path() /
        "matrix-performance.zidx";

    std::cout << "Database file: " << path << '\n';

    std::filesystem::remove(path);

    std::unordered_map<std::string, std::string> expected;
    expected.reserve(pair_count);

    // Generate data before timing database operations
    {
        for (std::size_t index = 0; index < pair_count; ++index) {
            expected.emplace(
                "key-" + std::to_string(index),
                "value-" + std::to_string(index));
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
    std::cout << "Put time:     "
            << put_duration.count()
            << " seconds\n";
    std::cout << "Put rate:     "
            << pair_count / put_duration.count()
            << " ops/second\n";
    std::cout << "Load time:    "
            << load_duration.count()
            << " seconds\n";
    std::cout << "Verify time:  "
            << verify_duration.count()
            << " seconds\n";
    std::cout << "Get rate:     "
            << pair_count / verify_duration.count()
            << " ops/second\n";
    std::cout << "File size:    "
            << file_size
            << " bytes\n";

    std::filesystem::remove(path);

    return 0;
}

}