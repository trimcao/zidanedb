#include <csignal>
#include <cstdlib>
#include <string_view>

inline void failpoint(std::string_view name) {
    const char* requested = std::getenv("ZIDANEDB_FAILPOINT");

    if (requested != nullptr && name == requested) {
        raise(SIGSTOP);
    }
}