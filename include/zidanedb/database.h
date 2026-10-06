#ifndef ZIDANEDB_DATABASE_H
#define ZIDANEDB_DATABASE_H

#include "zidanedb/index_stats.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace zidanedb {

enum class ScanStatus { Success, Truncated, InvalidLength, ChecksumMismatch, InvalidType };

struct ScanResult {
    ScanStatus status;
    std::uint64_t last_valid_record_offset;
    std::uint64_t failing_record_offset;
};

class Index;

enum class RecoveryReason {
    none,
    missing_index,
    invalid_index,
    unclean_index, // Dirty flag or checkpoint mismatch
};

enum class RecoveryAction {
    none,
    full_index_rebuild,
};

struct RecoveryReport {
    RecoveryReason reason{RecoveryReason::none};
    RecoveryAction action{RecoveryAction::none};
    std::uint64_t truncated_bytes{0};
};

class Database {
  private:
    std::filesystem::path db_path_;
    std::filesystem::path idx_path_;
    std::unique_ptr<Index> index_;

    std::string magic_;
    std::uint32_t version_;

    RecoveryReport open_recovery_report_{};

    bool closed_{false};
    void ensure_open() const;

    void load();
    void setup();
    void rebuild_index_impl(std::uint64_t start_offset, std::uint64_t bucket_count);

    std::uint64_t header_size() const;

  public:
    explicit Database(std::filesystem::path path, std::uint64_t num_index_buckets = 1'000'000);
    ~Database() noexcept;

    void close();

    [[nodiscard]]
    std::optional<std::string> get(const std::string& key) const;

    // put() succeeds or throws an exception.
    void put(const std::string& key, const std::string& val);

    // erase() reports whether the key existed.
    [[nodiscard]]
    bool erase(const std::string& key);

    IndexStats get_index_stats() const;
    bool index_metadata_clean() const;

    // scan all the data records, and find the longest valid prefix,
    ScanResult scan_records(std::uint64_t start_offset = 0);
    std::uint64_t recover_records();
    void rebuild_index(std::uint64_t start_offset = 0);

    const RecoveryReport& open_recovery_report() const noexcept { return open_recovery_report_; }
};

} // namespace zidanedb

#endif // ZIDANEDB_DATABASE_H
