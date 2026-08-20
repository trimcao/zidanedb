#include "matrix.h"
#include "utils.h"

namespace matrix::cli {

int test_basic() {
    using Clock = std::chrono::steady_clock;
    using Seconds = std::chrono::duration<double>;

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
                random_string(random_engine, 12);

            std::string value =
                random_string(random_engine, 32);

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

}