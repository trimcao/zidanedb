#include "zidanedb/database.h"
#include "constants.h"
#include "failpoints.h"
#include "index.h"
#include "record.h"
#include "utils.h"
#include "zidanedb/index_stats.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace {

struct TemporaryIndexLocation {
    std::filesystem::path directory;
    std::filesystem::path index_path;
};

TemporaryIndexLocation
make_temporary_index_location(const std::filesystem::path& final_index_path) {
    namespace fs = std::filesystem;

    auto parent = final_index_path.parent_path();
    if (parent.empty()) {
        parent = ".";
    }

    const auto filename = final_index_path.filename().string();
    std::random_device random;

    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto token = std::to_string(random()) + "-" + std::to_string(random());
        const auto directory = parent / ("." + filename + ".rebuild-" + token);

        std::error_code error;
        if (fs::create_directory(directory, error)) {
            return TemporaryIndexLocation{directory, directory / final_index_path.filename()};
        }

        // An existing name is only a collision, so try another candidate.
        if (!error || error == std::errc::file_exists) {
            continue;
        }

        throw fs::filesystem_error{"Could not create index staging directory", directory, error};
    }

    throw std::runtime_error{"Could not generate a unique temporary index path"};
}

} // namespace

namespace zidanedb {

Database::Database(std::filesystem::path path, std::uint64_t num_index_buckets)
    : db_path_{std::move(path)}, idx_path_{db_path_} {

    // Database names must end in .zdb; the companion index appends .idx.
    if (db_path_.extension() != ".zdb") {
        throw std::runtime_error("ZidaneDB database file must end with .zdb");
    }

    idx_path_ += ".idx";
    const bool database_exists = std::filesystem::exists(db_path_);
    const bool index_exists = std::filesystem::exists(idx_path_);

    if (!database_exists) {
        // With no data log there is nothing from which to rebuild a malformed
        // or populated index, so those errors must be reported to the caller.
        index_ = std::make_unique<Index>(idx_path_, num_index_buckets);
        if (!index_->empty()) {
            throw std::runtime_error("DB file does not exist but Index file is not empty");
        }
        setup();
        // initialize indexed_up_to_offset when creating a new db
        index_->set_indexed_up_to_offset(std::filesystem::file_size(db_path_));
    } else {
        load();

        bool index_rebuilt = false;
        if (index_exists) {
            try {
                index_ = std::make_unique<Index>(idx_path_, num_index_buckets);
            } catch (const InvalidIndexError&) {
                // A structurally invalid index is disposable: the data log is
                // the source of truth and can produce a replacement.
                rebuild_index_impl(0, num_index_buckets);
                index_rebuilt = true;
            }
        } else {
            rebuild_index_impl(0, num_index_buckets);
            index_rebuilt = true;
        }

        if (!index_rebuilt && !index_metadata_clean()) {
            // Basic recovery policy:
            // - Trigger basic recovery when the Index metadata is not clean.
            // - Check for incomplete tail in the db file, then truncate if required.
            // - Rebuild index.
            recover_records();
        }
    }

    // set index_clean to false before working with the database
    index_->set_index_clean(false);
}

Database::~Database() noexcept {
    try {
        close();
    } catch (...) {
        // Destructors cannot safely report an exception.
        // The index should remain marked dirty, so the next
        // open performs recovery.
    }
}

void Database::close() {
    if (closed_) {
        return;
    }

    // Only declare the database clean if the index represents the complete
    // data log.
    if (std::filesystem::file_size(db_path_) != index_->indexed_up_to_offset()) {
        throw std::runtime_error{"Cannot close database: index is not caught up"};
    }

    ZIDANEDB_FAILPOINT("before_set_index_clean");

    index_->set_index_clean(true);
    // release the in-memory index object
    index_.reset();

    closed_ = true;

    ZIDANEDB_FAILPOINT("clean_db_close");
}

std::optional<std::string> Database::get(const std::string& key) const {
    ensure_open();

    Record record{};

    // Index membership distinguishes a missing key from a stored empty value.
    auto offset = index_->find(key);
    if (!offset) {
        return std::nullopt;
    }

    std::ifstream file{db_path_, std::ios::binary};
    if (!file) {
        throw std::runtime_error("Could not open the file");
    }
    file.seekg(*offset, std::ios::beg);
    if (!file) {
        throw std::runtime_error("Seek failed");
    }

    if (read_record(file, record) != RecordReadStatus::Success) {
        throw std::runtime_error("Cannot get the db record");
    }
    if (record.type != RecordType::Put) {
        throw std::runtime_error("Record type must be PUT");
    }
    if (record.key != key) {
        throw std::runtime_error("Key in record is not equal to the requested key");
    }

    return record.value;
}

void Database::put(const std::string& key, const std::string& val) {
    ensure_open();

    // Writes are append-only, even when replacing a value with the same contents.
    if (key.size() > MAX_KEY_SIZE) {
        throw std::runtime_error("Key size exceeds max allowed key size");
    }
    if (val.size() > MAX_VALUE_SIZE) {
        throw std::runtime_error("Value size exceeds max allowed value size");
    }

    ZIDANEDB_FAILPOINT("before_db_append");

    Record record{RecordType::Put, key, val};
    std::ofstream file;
    std::uint64_t db_start_offset;
    std::uint64_t db_end_offset;

    try {
        file.open(db_path_, std::ios::binary | std::ios::app);
        file.exceptions(std::ios::failbit | std::ios::badbit);

        // get the offset of this record
        const auto start_position = file.tellp();
        if (start_position == std::ostream::pos_type(-1)) {
            throw std::runtime_error{"tellp() failed"};
        }
        db_start_offset = static_cast<std::uint64_t>(static_cast<std::streamoff>(start_position));

        write_record(file, record);
        const auto end_position = file.tellp();
        if (end_position == std::ostream::pos_type(-1)) {
            throw std::runtime_error{"tellp() failed"};
        }
        db_end_offset = static_cast<std::uint64_t>(static_cast<std::streamoff>(end_position));

        file.flush();
    } catch (const std::ios_base::failure& error) {
        throw std::runtime_error{"Could not write database file: " + db_path_.string()};
    }

    ZIDANEDB_FAILPOINT("after_db_append");

    // Last write wins: update the index only after flushing the record.
    // If this update fails, the appended record may remain unindexed.
    index_->set(key, db_start_offset);

    ZIDANEDB_FAILPOINT("before_update_indexed_up_to_offset");

    index_->set_indexed_up_to_offset(db_end_offset);

    ZIDANEDB_FAILPOINT("after_update_indexed_up_to_offset");
}

bool Database::erase(const std::string& key) {
    ensure_open();

    auto exist = index_->find(key);

    Record record{RecordType::Delete, key, ""};
    std::ofstream file;
    std::uint64_t db_end_offset;

    if (exist) {
        try {
            file.open(db_path_, std::ios::binary | std::ios::app);
            file.exceptions(std::ios::failbit | std::ios::badbit);

            // Deletion records retain the key and serialize an empty value.
            write_record(file, record);
            const auto end_position = file.tellp();
            if (end_position == std::ostream::pos_type(-1)) {
                throw std::runtime_error{"tellp() failed"};
            }
            db_end_offset = static_cast<std::uint64_t>(static_cast<std::streamoff>(end_position));

            file.flush();

        } catch (const std::ios_base::failure& error) {
            throw std::runtime_error{"Could not write database file: " + db_path_.string()};
        }

        // Unlink only after flushing the deletion record. If unlinking fails,
        // the index can still expose the old value until recovery is implemented.
        if (!index_->erase(key)) {
            throw std::runtime_error("Key " + key + " exists in db file but not in index file");
        }
        index_->set_indexed_up_to_offset(db_end_offset);
    }

    return exist.has_value();
}

IndexStats Database::get_index_stats() const {
    ensure_open();
    return index_->stats();
}

void Database::load() {
    if (!std::filesystem::exists(db_path_)) {
        throw std::runtime_error("Database file does not exist");
    }

    std::ifstream file{db_path_, std::ios::binary};
    if (!file) {
        throw std::runtime_error("Could not open database file: " + db_path_.string());
    }

    if ((utils::read_string(file, magic_, DB_MAGIC.size()) != utils::ReadStatus::Success) ||
        (magic_ != DB_MAGIC)) {
        throw std::runtime_error("Invalid ZidaneDB Database file");
    }
    if ((utils::read_uint32(file, version_) != utils::ReadStatus::Success) ||
        (version_ != DB_VERSION)) {
        throw std::runtime_error("Unsupported ZidaneDB Database version");
    }
}

void Database::setup() {
    if (std::filesystem::exists(db_path_)) {
        return;
    }

    magic_ = DB_MAGIC;
    version_ = DB_VERSION;

    try {
        std::ofstream file{db_path_, std::ios::binary};
        if (!file) {
            throw std::runtime_error("Could not open the file " + db_path_.string());
        }
        file.exceptions(std::ios::failbit | std::ios::badbit);

        utils::write_string(file, magic_, DB_MAGIC.size());
        utils::write_uint32(file, version_);

        file.flush();
    } catch (const std::ios_base::failure& error) {
        throw std::runtime_error{"Could not update database file: " + db_path_.string()};
    }
}

std::uint64_t Database::header_size() const {
    return utils::string_size(magic_) + sizeof(version_);
}

ScanResult Database::scan_records(std::uint64_t start_offset) {
    ensure_open();

    std::ifstream file{db_path_, std::ios::binary};
    if (!file) {
        throw std::runtime_error("Could not open the file");
    }

    // set exception so underlying failures throw automatically
    file.exceptions(std::ios::badbit);

    // TODO: maybe I need to verify start_offset value
    if (start_offset == 0) {
        start_offset = header_size();
    }

    file.seekg(start_offset, std::ios::beg);
    if (!file) {
        throw std::runtime_error("Seek failed");
    }

    // start reading records
    Record record{};
    RecordReadStatus read_status{};
    auto record_start = start_offset;
    auto record_end = file.tellg();
    std::uint64_t last_valid_record_offset = 0;
    std::uint64_t failing_record_offset = 0;
    while ((read_status = read_record(file, record)) == RecordReadStatus::Success) {
        last_valid_record_offset = record_start;
        record_end = file.tellg();
        record_start = record_end;
    }

    ScanStatus scan_status{};
    switch (read_status) {
    case RecordReadStatus::ChecksumMismatch:
        scan_status = ScanStatus::ChecksumMismatch;
        failing_record_offset = record_start;
        break;
    case RecordReadStatus::InvalidLength:
        scan_status = ScanStatus::InvalidLength;
        failing_record_offset = record_start;
        break;
    case RecordReadStatus::InvalidType:
        scan_status = ScanStatus::InvalidType;
        failing_record_offset = record_start;
        break;
    case RecordReadStatus::Truncated:
        scan_status = ScanStatus::Truncated;
        failing_record_offset = record_start;
        break;
    case RecordReadStatus::EndOfFile:
    case RecordReadStatus::Success:
        scan_status = ScanStatus::Success;
        break;
    }

    return ScanResult{scan_status, last_valid_record_offset, failing_record_offset};
}

void Database::recover_records() {
    ensure_open();

    // TODO: backup db file?

    auto scan_result = scan_records();

    switch (scan_result.status) {
    case ScanStatus::Success:
        // std::cout << "db file is healthy\n";
        break;
    case ScanStatus::Truncated:
        std::cout << "truncating...\n";
        std::filesystem::resize_file(db_path_, scan_result.failing_record_offset);
        break;
    default:
        throw std::runtime_error("db file is corrupted");
    }

    // TODO: rescan and verify?

    rebuild_index();
}

void Database::rebuild_index(std::uint64_t start_offset) {
    ensure_open();
    rebuild_index_impl(start_offset, index_->num_buckets());
}

void Database::rebuild_index_impl(std::uint64_t start_offset, std::uint64_t bucket_count) {

    std::ifstream file{db_path_, std::ios::binary};
    if (!file) {
        throw std::runtime_error("Could not open the file");
    }

    // set exception so underlying failures throw automatically
    file.exceptions(std::ios::badbit);

    if (start_offset == 0) {
        start_offset = header_size();
    }

    file.seekg(start_offset, std::ios::beg);
    if (!file) {
        throw std::runtime_error("Seek failed");
    }

    const auto temporary = make_temporary_index_location(idx_path_);

    try {
        {
            Index temporary_index{temporary.index_path, bucket_count};

            Record record{};
            RecordReadStatus read_status{};
            auto record_start = start_offset;
            auto record_end = file.tellg();
            while ((read_status = read_record(file, record)) == RecordReadStatus::Success) {
                record_end = file.tellg();

                switch (record.type) {
                case RecordType::Put:
                    temporary_index.set(record.key, record_start);
                    break;
                case RecordType::Delete:
                    static_cast<void>(temporary_index.erase(record.key));
                    break;
                }

                record_start = record_end;
            }

            if (read_status != RecordReadStatus::EndOfFile) {
                throw std::runtime_error("db file is corrupted, cannot rebuild index");
            }
        }

        // Install the completed index, then create an owner for the final path.
        std::filesystem::rename(temporary.index_path, idx_path_);
        index_ = std::make_unique<Index>(idx_path_, bucket_count);

        std::error_code cleanup_error;
        std::filesystem::remove(temporary.directory, cleanup_error);
    } catch (...) {
        std::error_code cleanup_error;
        std::filesystem::remove_all(temporary.directory, cleanup_error);
        throw;
    }

    // set indexed_up_to_offset
    index_->set_indexed_up_to_offset(std::filesystem::file_size(db_path_));
}

bool Database::index_metadata_clean() const {
    ensure_open();
    return index_->index_clean() &&
           std::filesystem::file_size(db_path_) == index_->indexed_up_to_offset();
}

void Database::ensure_open() const {
    if (closed_ || !index_) {
        throw std::logic_error("Database is closed");
    }
}

} // namespace zidanedb
