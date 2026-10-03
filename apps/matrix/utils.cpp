#include "utils.h"
#include <filesystem>
#include <string_view>

namespace matrix::tests::detail {

std::string random_string(std::mt19937_64& random_engine, std::size_t length) {
    constexpr std::string_view characters{"abcdefghijklmnopqrstuvwxyz"
                                          "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                          "0123456789"};

    std::uniform_int_distribution<std::size_t> pick{0, characters.size() - 1};

    std::string result;
    result.reserve(length);

    for (std::size_t index = 0; index < length; ++index) {
        result.push_back(characters[pick(random_engine)]);
    }

    return result;
}

TemporaryDirectory::TemporaryDirectory() {
    const auto parent = std::filesystem::temp_directory_path();
    std::random_device random;

    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto token = std::to_string(random()) + "-" + std::to_string(random());
        const auto candidate = parent / ("zidanedb-crash-" + token);

        std::error_code error;
        if (std::filesystem::create_directory(candidate, error)) {
            path_ = candidate;
            return;
        }

        if (!error || error == std::errc::file_exists) {
            continue;
        }

        throw std::filesystem::filesystem_error{"Could not create temporary directory", candidate,
                                                error};
    }

    throw std::runtime_error{"Could not generate a unique temporary directory"};
}

TemporaryDirectory::~TemporaryDirectory() noexcept {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
}

const std::filesystem::path& TemporaryDirectory::path() const noexcept { return path_; }

} // namespace matrix::tests::detail