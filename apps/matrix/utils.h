#ifndef MATRIX_UTILS_H
#define MATRIX_UTILS_H

#include <cstddef>
#include <filesystem>
#include <random>
#include <string>
#include <sys/types.h>

namespace matrix::utils {

std::string random_string(std::mt19937_64& random_engine, std::size_t length);

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

class ChildProcess {
  public:
    explicit ChildProcess(pid_t pid);

    ~ChildProcess() noexcept { cleanup(); }

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    bool wait_for_state(int& status);
    bool kill_and_reap(int& status);
    void mark_reaped() noexcept;

  private:
    pid_t pid_;
    bool reaped_{false};
    void cleanup() noexcept;
};

} // namespace matrix::utils

#endif // MATRIX_UTILS_H