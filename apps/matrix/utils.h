#ifndef MATRIX_UTILS_H
#define MATRIX_UTILS_H

#include <chrono>
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

enum class WaitOutcome {
    state_changed,
    timed_out,
    error,
};

class ChildProcess {
  public:
    explicit ChildProcess(pid_t pid);

    ~ChildProcess() noexcept { cleanup(); }

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    WaitOutcome wait_for_state(int& status, std::chrono::milliseconds timeout);
    bool kill_and_reap(int& status);

  private:
    pid_t pid_;
    bool reaped_{false};
    void cleanup() noexcept;
};

} // namespace matrix::utils

#endif // MATRIX_UTILS_H