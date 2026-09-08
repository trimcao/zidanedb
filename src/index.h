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

    std::string magic;
    std::string version;
    std::uint32_t num_buckets;
    std::uint64_t entry_start_offset;

    void load();
};

struct IndexEntry {
    std::uint64_t db_offset;
    std::uint64_t next_entry_offset;
    std::string key;
};

struct IndexEntryHeader {
    std::uint32_t key_length;
    std::uint64_t db_offset;
    std::uint64_t next_entry_offset;
};

} // namespace zidanedb

#endif