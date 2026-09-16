// Note: this header file is under apps/zidane and not include/ because
// this header contains private implementation of zidane cli.
// The root include/ is meant for public database API.

#ifndef ZIDANE_COMMANDS_H
#define ZIDANE_COMMANDS_H

#include "zidanedb/database.h"

#include <ostream>
#include <string>

namespace zidanedb::cli {

int run_put(Database& database, std::string key, std::string value, std::ostream& output);

int run_get(const Database& database, const std::string& key, std::ostream& output,
            std::ostream& error);

int run_delete(Database& database, const std::string& key, std::ostream& output,
               std::ostream& error);

} // namespace zidanedb::cli

#endif // ZIDANE_COMMANDS_H
