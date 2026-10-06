#include "matrix.h"
#include "utils.h"
#include "zidanedb/database.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace matrix::tests {

int run_crash_test(std::string& failpoint) {
    /*
    Plan:
    - Create a child process
    - Pass the ZIDANEDB_FAILPOINT env var
    - Execute the child process
    - Kill the child process
    - Reopen the db and see how it recovers
    */

    using namespace std::chrono_literals;

    // create unique directory
    matrix::utils::TemporaryDirectory workspace;

    const auto path = workspace.path() / "database.zdb";
    const auto idx_path = workspace.path() / "database.zdb.idx";

    // expected map
    std::unordered_map<std::string, std::string> expected;
    expected.emplace("team", "madrid");
    if (failpoint != "before_db_append") {
        expected.emplace("player", "bellingham");
    }

    const std::optional<std::string> expected_player =
        failpoint == "before_db_append" ? std::nullopt : std::optional<std::string>{"bellingham"};

    // initialize a database
    {
        zidanedb::Database database{path};
        database.put("team", "madrid");
        if (failpoint == "after_modify_existing_entry") {
            database.put("player", "benzema");
        }
    }

    const std::filesystem::path child_bin{ZIDANEDB_CRASH_WORKER_PATH};

    // Convert to a C-style string for exec
    std::string binary_str = child_bin.string();

    std::vector<const char*> args;
    args.push_back(binary_str.c_str()); // The first argument is conventionally the program name
    args.push_back("--db");
    args.push_back(path.c_str());
    args.push_back("put");
    args.push_back("player");
    args.push_back("bellingham");
    args.push_back(nullptr); // The array MUST be null-terminated

    bool reached_failpoint = false;

    pid_t pid = fork();

    if (pid < 0) {
        std::cerr << "Fork failed!\n";
        return 1;
    } else if (pid == 0) {
        // Child Process
        setenv("ZIDANEDB_FAILPOINT", failpoint.c_str(), 1);

        // cast to char* const* because execvp expects a mutable array of pointers
        execvp(args[0], const_cast<char* const*>(args.data()));

        // If execvp returns, it failed
        std::cerr << "Exec failed!" << std::endl;
        printf("Executing command: ");
        for (int i = 0; args[i] != NULL; i++) {
            printf("%s ", args[i]);
        }
        printf("\n");
        // use _exit() to avoid flushing copies of the parent's buffered streams
        _exit(EXIT_FAILURE);
    } else {
        // Parent Process
        std::cout << "Spawned Zidane process with PID: " << pid << std::endl;

        utils::ChildProcess child{pid};

        int status;

        // Use WUNTRACED to monitor if the child gets stopped (e.g., SIGSTOP)
        while (true) {
            const auto outcome = child.wait_for_state(status, 5s);
            switch (outcome) {
            case utils::WaitOutcome::state_changed:
                break;

            case utils::WaitOutcome::timed_out:
                std::cerr << "Timed out waiting for Zidane crash worker\n";
                if (!child.kill_and_reap(status)) {
                    std::cerr << "Could not kill and reap Zidane crash worker\n";
                }
                return 1;

            case utils::WaitOutcome::error:
                std::cerr << "waitpid failed\n";
                return 1;
            }

            // Check if the child process was stopped by a signal
            if (WIFSTOPPED(status)) {
                int stop_sig = WSTOPSIG(status);
                std::cout << "Zidane process stopped by signal: " << stop_sig << std::endl;

                if (stop_sig != SIGSTOP) {
                    std::cout << "Zidane process is not stopped by SIGSTOP" << stop_sig
                              << std::endl;
                    return 1;
                } else {
                    std::cout << "Zidane process was stopped. Sending SIGKILL to terminate it..."
                              << std::endl;

                    if (!child.kill_and_reap(status)) {
                        std::cerr << "Could not kill and reap Zidane process\n";
                        return 1;
                    }

                    std::cout << "Zidane process has been successfully killed." << std::endl;
                    reached_failpoint = true;
                    break;
                }
            }

            // Check if the child exited naturally or was killed by something
            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                std::cout << "Zidane process finished." << std::endl;
                break;
            }
        }
    }

    if (!reached_failpoint) {
        std::cerr << "Zidane process did not reach failpoint\n" << std::endl;
        return 1;
    }

    // Reopening the database and check
    {
        const zidanedb::Database database{path};

        // Only clean_db_close runs after the index has been marked clean.
        const bool expects_recovery = failpoint != "clean_db_close";
        const auto expected_reason = expects_recovery ? zidanedb::RecoveryReason::unclean_index
                                                      : zidanedb::RecoveryReason::none;
        const auto expected_action = expects_recovery ? zidanedb::RecoveryAction::full_index_rebuild
                                                      : zidanedb::RecoveryAction::none;
        const auto& report = database.open_recovery_report();

        if (report.reason != expected_reason) {
            std::cerr << "Unexpected recovery reason for failpoint '" << failpoint << "': expected "
                      << (expects_recovery ? "unclean_index" : "none") << '\n';
            return 1;
        }
        if (report.action != expected_action) {
            std::cerr << "Unexpected recovery action for failpoint '" << failpoint << "': expected "
                      << (expects_recovery ? "full_index_rebuild" : "none") << '\n';
            return 1;
        }

        // These failpoints never leave an incomplete data record.
        if (report.truncated_bytes != 0) {
            std::cerr << "Unexpected truncation for failpoint '" << failpoint << "': removed "
                      << report.truncated_bytes << " bytes, expected 0\n";
            return 1;
        }

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

        if (database.get("player") != expected_player) {
            if (expected_player) {
                std::cerr << "Key player should exist" << '\n';
            } else {
                std::cerr << "Key player should not exist" << '\n';
            }
            return 1;
        }
    }

    std::cout
        << "PASS: crash scenario matched the expected recovery report and database contents\n";

    return 0;
}

} // namespace matrix::tests
