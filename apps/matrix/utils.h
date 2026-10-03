#ifndef MATRIX_UTILS_H
#define MATRIX_UTILS_H

#include <cstddef>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace matrix::tests::detail {

std::string random_string(std::mt19937_64& random_engine, std::size_t length);

// class TemporaryDirectory {
//   public:
//     explicit TemporaryDirectory(std::filesystem::path path) : path_{std::move(path)} {
//         if (!std::filesystem::create_directory(path_)) {
//             throw std::runtime_error{"Could not create temporary directory"};
//         }
//     }

//     ~TemporaryDirectory() noexcept {
//         std::error_code ignored;
//         std::filesystem::remove_all(path_, ignored);
//     }

//     const std::filesystem::path& path() const noexcept { return path_; }

//   private:
//     std::filesystem::path path_;
// };

class TemporaryDirectory {
  public:
    TemporaryDirectory();
    ~TemporaryDirectory() noexcept;

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    TemporaryDirectory(TemporaryDirectory&&) = delete;
    TemporaryDirectory& operator=(TemporaryDirectory&&) = delete;

    const std::filesystem::path& path() const noexcept;

  private:
    std::filesystem::path path_;
};

std::filesystem::path make_unique_temporary_location();

} // namespace matrix::tests::detail

#endif // MATRIX_UTILS_H