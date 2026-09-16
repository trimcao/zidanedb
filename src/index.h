#ifndef ZIDANEDB_INDEX_H
#define ZIDANEDB_INDEX_H

#include "zidanedb/index_stats.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace zidanedb {

struct EntryLocation {
    // Matching entry's offset, or zero if the key is absent.
    std::uint64_t entry_offset;
    // Predecessor's offset, or zero if the match is at the head or the key is absent.
    std::uint64_t prev_entry_offset;
    // Location of the bucket selected by the key's hash.
    std::uint64_t bucket_offset;
    // Head of the bucket's collision chain, or zero for an empty bucket.
    std::uint64_t chain_offset;
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

class Index {
  public:
    explicit Index(std::filesystem::path path, std::uint64_t num_buckets = 1'000'000);

    [[nodiscard]]
    std::optional<std::uint64_t> find(const std::string& key) const;

    void set(const std::string& key, std::uint64_t db_offset);

    [[nodiscard]]
    bool erase(const std::string& key);

    IndexStats stats() const;
    bool empty() const;

  private:
    std::filesystem::path path_;

    std::string magic_;
    std::uint32_t version_;
    std::uint64_t num_buckets_;

    void load();
    void setup();
    std::uint64_t index_size_before_entries() const;
    std::uint64_t header_size() const;
    EntryLocation find_entry_offset(std::istream& file, const std::string& key) const;
    IndexEntry read_entry(std::istream& file, std::uint64_t entry_offset) const;
};

} // namespace zidanedb

#endif
