#include "commands.h"

#include <utility>

namespace zidanedb::cli {

int run_put(Database& database, std::string key, std::string value, std::ostream& output) {
    database.put(std::move(key), std::move(value));
    output << "Value stored\n";
    return 0;
}

int run_get(const Database& database, const std::string& key, std::ostream& output,
            std::ostream& error) {
    const auto val = database.get(key);
    if (!val.has_value()) {
        error << "Key not found\n";
        return 1;
    }
    output << *val << "\n";
    return 0;
}

int run_delete(Database& database, const std::string& key, std::ostream& output,
               std::ostream& error) {
    auto existed = database.erase(key);
    if (!existed) {
        error << "Key " << key << " does not exist" << "\n";
        return 1;
    }
    output << "Key " << key << " deleted" << "\n";
    return 0;
}

} // namespace zidanedb::cli