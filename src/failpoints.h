#ifndef ZIDANEDB_FAILPOINTS_H
#define ZIDANEDB_FAILPOINTS_H

#ifdef ZIDANEDB_ENABLE_FAILPOINTS

#include <csignal>
#include <cstdlib>
#include <string_view>

namespace zidanedb::testing {

inline void failpoint(std::string_view name) {
    const char* requested = std::getenv("ZIDANEDB_FAILPOINT");

    if (requested != nullptr && name == requested) {
        raise(SIGSTOP);
    }
}

} // namespace zidanedb::testing

#define ZIDANEDB_FAILPOINT(name) ::zidanedb::testing::failpoint(name)

#else

#define ZIDANEDB_FAILPOINT(name) ((void)0)

#endif

#endif