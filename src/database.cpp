#include "zidanedb/database.h"

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
    // TODO: why we need std::move() here
    data_.insert_or_assign(std::move(key), std::move(val));
}

bool Database::erase(const std::string& key)
{
    return data_.erase(key);
}

}