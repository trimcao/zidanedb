#include "zidanedb/database.h"
#include <utility>

namespace zidanedb {

Database::Database(std::filesystem::path path)
{
    path_ = std::move(path);
}

std::optional<std::string>
Database::get(const std::string& key) const
{
    auto it = data_.find(key);
    if (it == data_.end()) {
        return std::nullopt;
    }

    return it->second;
}

void Database::put(std::string key, std::string val)
{
    // note about std::move():
    // std::move() gives permission to transfer resources from an object because
    // its current value is no longer needed.
    // std::move() itself does not perform the transfer. It marks the object as movable;
    // the receiving constructor or function decides what happens.
    data_.insert_or_assign(std::move(key), std::move(val));
}

bool Database::erase(const std::string& key)
{
    return data_.erase(key);
}

}