#ifndef ZIDANEDB_DATABASE_H
#define ZIDANEDB_DATABASE_H

#include <filesystem>
#include <optional>
#include <string>

namespace zidanedb {

class Index;

class Database {
  private:
    std::filesystem::path db_path_;
    std::filesystem::path idx_path_;
    // std::unordered_map<std::string, std::uint64_t> index_;
    std::unique_ptr<Index> index_;

  public:
    explicit Database(std::filesystem::path path);
    explicit Database(std::filesystem::path path, std::uint64_t num_index_buckets);
    ~Database();

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
