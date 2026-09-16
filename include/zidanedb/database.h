#ifndef ZIDANEDB_DATABASE_H
#define ZIDANEDB_DATABASE_H

#include "zidanedb/index_stats.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace zidanedb {

class Index;

class Database {
  private:
    std::filesystem::path db_path_;
    std::filesystem::path idx_path_;
    std::unique_ptr<Index> index_;

  public:
    explicit Database(std::filesystem::path path, std::uint64_t num_index_buckets = 1'000'000);
    ~Database();

    [[nodiscard]]
    std::optional<std::string> get(const std::string& key) const;

    // put() succeeds or throws an exception.
    void put(const std::string& key, const std::string& val);

    // erase() reports whether the key existed.
    [[nodiscard]]
    bool erase(const std::string& key);

    IndexStats get_index_stats() const;
};

} // namespace zidanedb

#endif // ZIDANEDB_DATABASE_H
