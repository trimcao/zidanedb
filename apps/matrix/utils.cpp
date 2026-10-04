#include "utils.h"
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <signal.h>
#include <stdexcept>
#include <string_view>
#include <sys/wait.h>
#include <thread>

namespace matrix::utils {

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

ChildProcess::ChildProcess(pid_t pid) : pid_{pid} {
    if (pid_ <= 0) {
        throw std::invalid_argument{"ChildProcess requires a positive PID"};
    }
}

bool ChildProcess::kill_and_reap(int& status) {
    if (reaped_) {
        return true;
    }

    if (::kill(pid_, SIGKILL) == -1) {
        return false;
    }

    pid_t result;
    do {
        result = ::waitpid(pid_, &status, 0);
    } while (result == -1 && errno == EINTR);

    if (result != pid_) {
        return false;
    }

    reaped_ = true;

    if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL) {
        return false;
    }

    return true;
}

void ChildProcess::cleanup() noexcept {
    if (reaped_ || pid_ <= 0) {
        return;
    }

    // Destructor cleanup is a last-resort, best-effort fallback
    static_cast<void>(::kill(pid_, SIGKILL));

    int status{};
    while (::waitpid(pid_, &status, 0) == -1 && errno == EINTR) {
    }

    reaped_ = true;
}

WaitOutcome ChildProcess::wait_for_state(int& status, std::chrono::milliseconds timeout) {
    if (reaped_) {
        return WaitOutcome::error;
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;

    while (true) {
        const pid_t result = ::waitpid(pid_, &status, WUNTRACED | WNOHANG);

        // When waiting for one specific child:
        // result == pid should be true.
        if (result == pid_) {
            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                reaped_ = true;
            }

            return WaitOutcome::state_changed;
        }

        if (result == -1) {
            if (errno == EINTR) {
                // continue if being interrupted
                if (std::chrono::steady_clock::now() >= deadline) {
                    return WaitOutcome::timed_out;
                }
                continue;
            }

            return WaitOutcome::error;
        }

        // result == 0 means the child has no reportable state change
        if (std::chrono::steady_clock::now() >= deadline) {
            return WaitOutcome::timed_out;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
}

} // namespace matrix::utils