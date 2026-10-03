#include "matrix.h"
#include "utils.h"
#include "zidanedb/database.h"
#include <filesystem>
#include <iostream>
#include <random>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace matrix::tests {

int run_crash_test(const char* binary_path, std::string& failpoint) {
    /*
    Plan:
    - Create a child process
    - Pass the ZIDANEDB_FAILPOINT env var
    - Execute the child process
    - Kill the child process
    - Reopen the db and see how it recovers
    */

    // create unique directory
    matrix::tests::detail::TemporaryDirectory workspace;

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
    }

    // Parent figures out its own directory once, then passes it
    // Converts "./parent" or "parent" into an absolute path based on launch context
    std::filesystem::path parent_bin = std::filesystem::absolute(binary_path);
    std::filesystem::path parent_dir = parent_bin.parent_path();

    std::filesystem::path child_bin = parent_dir / "zidane_crash_worker";

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
        exit(EXIT_FAILURE);
    } else {
        // Parent Process
        std::cout << "Spawned Zidane process with PID: " << pid << std::endl;

        int status;

        // Use WUNTRACED to monitor if the child gets stopped (e.g., SIGSTOP)
        while (true) {
            pid_t result = waitpid(pid, &status, WUNTRACED);
            if (result == -1) {
                std::cerr << "waitpid failed" << std::endl;
                break;
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
                    if (kill(pid, SIGKILL) != 0) {
                        std::cout << "error: kill\n";
                        return 1;
                    }

                    // Harvest the child's final status after killing it
                    const auto wait_result = waitpid(pid, &status, 0);
                    if (wait_result != pid) {
                        std::cerr << "error: waitpid\n";
                        return 1;
                    }

                    if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL) {
                        std::cout << "Zidane process was not killed properly by SIGKILL"
                                  << std::endl;
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
        std::cout << "Zidane process did not reach failpoint\n" << std::endl;
        return 1;
    }

    // Reopening the database and check
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

        if (database.get("player") != expected_player) {
            if (expected_player) {
                std::cerr << "Key player should exist" << '\n';
            } else {
                std::cerr << "Key player should not exist" << '\n';
            }
            return 1;
        }
    }

    std::cout << "PASS: database recovered successfully from the crash point\n";

    return 0;
}

} // namespace matrix::tests