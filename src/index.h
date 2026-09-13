#ifndef ZIDANEDB_INDEX_H
#define ZIDANEDB_INDEX_H

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace zidanedb {

class Index {
  public:
    explicit Index(std::filesystem::path path, std::uint64_t num_buckets = 1'000'000);

    [[nodiscard]]
    std::optional<std::uint64_t> find(const std::string& key) const;

    void set(std::string key, std::uint64_t db_offset);

    [[nodiscard]]
    bool erase(const std::string& key);

  private:
    std::filesystem::path path_;
    // std::unordered_map<std::string, std::uint64_t> offsets_;

    std::string magic_;
    std::uint32_t version_;
    std::uint64_t num_buckets_;

    void load();
    void setup();
    std::uint64_t get_start_entry_offset();
};

struct IndexEntry {
    std::uint64_t db_offset;
    std::uint64_t next_entry_offset;
    std::string key;
};

struct IndexEntryHeader {
    std::uint64_t db_offset;
    std::uint64_t next_entry_offset;
};

} // namespace zidanedb

#endif