#ifndef ZIDANEDB_DATABASE_H
#define ZIDANEDB_DATABASE_H

#include <string>
#include <unordered_map>
#include <filesystem>
#include <optional>
#include <cstdint>

namespace zidanedb {

class Database {
private:
    std::filesystem::path db_path_;
    std::filesystem::path idx_path_;
    // std::unordered_map<std::string, std::string> data_;
    std::unordered_map<std::string, uint64_t> index_;

public:
    explicit Database(
        std::filesystem::path path,
        std::filesystem::path idx_path
    );

    [[nodiscard]]
    std::optional<std::string> get(const std::string& key) const;

    // note: we use exception-based design for put
    // put() succeeds or throws an exception.
    void put(std::string key, std::string val);

    // erase() reports whether the key existed.
    [[nodiscard]]
    bool erase(const std::string& key);
};

} // namespace zidanedb

#endif // ZIDANEDB_DATABASE_H
