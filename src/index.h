#ifndef ZIDANEDB_INDEX_H
#define ZIDANEDB_INDEX_H

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

namespace zidanedb {

class Index {
public:
    explicit Index(std::filesystem::path path);

    [[nodiscard]]
    std::optional<std::uint64_t> find(const std::string& key) const;

    void set(std::string key, std::uint64_t db_offset);

    [[nodiscard]]
    bool erase(const std::string& key);

private:
    std::filesystem::path path_;
    std::unordered_map<std::string, std::uint64_t> offsets_;

    void load();
};

} // namespace zidanedb

#endif