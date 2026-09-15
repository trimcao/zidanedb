#ifndef ZIDANEDB_INDEX_H
#define ZIDANEDB_INDEX_H

#include "zidanedb/index_stats.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace zidanedb {

struct EntryLocation {
    // entry offset for a given key
    std::uint64_t entry_offset;
    // prev entry offset of the found entry offset
    std::uint64_t prev_entry_offset;
    // bucket offset (location of the bucket given the hash value)
    std::uint64_t bucket_offset;
    // start of the collision chain (the offset indicated by the bucket)
    std::uint64_t chain_offset;
};

class Index {
  public:
    explicit Index(std::filesystem::path path, std::uint64_t num_buckets = 1'000'000);

    [[nodiscard]]
    std::optional<std::uint64_t> find(const std::string& key) const;

    void set(std::string key, std::uint64_t db_offset);

    [[nodiscard]]
    bool erase(const std::string& key);

    IndexStats stats();

  private:
    std::filesystem::path path_;
    // std::unordered_map<std::string, std::uint64_t> offsets_;

    std::string magic_;
    std::uint32_t version_;
    std::uint64_t num_buckets_;

    void load();
    void setup();
    std::uint64_t get_start_entry_offset();
    EntryLocation find_entry_offset(const std::string& key) const;
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
