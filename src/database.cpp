#include "zidanedb/database.h"
#include "constants.h"
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
#include <stdexcept>
#include <string>
#include <utility>

namespace zidanedb {

struct RecordScanResult {
    RecordReadStatus status;
    std::uint64_t last_valid_offset;
    std::uint64_t failing_record_offset;
};

Database::Database(std::filesystem::path path, std::uint64_t num_index_buckets)
    : db_path_{std::move(path)}, idx_path_{db_path_} {

    // Database names must end in .zdb; the companion index appends .idx.
    if (db_path_.extension() != ".zdb") {
        throw std::runtime_error("ZidaneDB database file must end with .zdb");
    }

    idx_path_ += ".idx";
    // check if the db file exists but the index file does not
    if (std::filesystem::exists(db_path_) && !std::filesystem::exists(idx_path_)) {
        throw std::runtime_error("DB file exists but Index file does not exist");
    }

    index_ = std::make_unique<Index>(idx_path_, num_index_buckets);
    // check if the db file does not exist but the index file has contents
    if (!std::filesystem::exists(db_path_) && !index_->empty()) {
        throw std::runtime_error("DB file does not exist but Index file is not empty");
    }

    // load/setup the db file
    if (std::filesystem::exists(db_path_)) {
        load();
    } else {
        setup();
    }
}

Database::~Database() = default;

std::optional<std::string> Database::get(const std::string& key) const {
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
    // Writes are append-only, even when replacing a value with the same contents.
    if (key.size() > MAX_KEY_SIZE) {
        throw std::runtime_error("Key size exceeds max allowed key size");
    }
    if (val.size() > MAX_VALUE_SIZE) {
        throw std::runtime_error("Value size exceeds max allowed value size");
    }

    Record record{RecordType::Put, key, val};
    std::ofstream file;
    std::uint64_t db_offset;

    try {
        file.open(db_path_, std::ios::binary | std::ios::app);
        file.exceptions(std::ios::failbit | std::ios::badbit);

        // get the offset of this record
        const auto position = file.tellp();
        if (position == std::ostream::pos_type(-1)) {
            throw std::runtime_error{"tellp() failed"};
        }
        db_offset = static_cast<std::uint64_t>(static_cast<std::streamoff>(position));

        write_record(file, record);
        file.flush();
    } catch (const std::ios_base::failure& error) {
        throw std::runtime_error{"Could not write database file: " + db_path_.string()};
    }

    // Last write wins: update the index only after flushing the record.
    // If this update fails, the appended record may remain unindexed.
    index_->set(key, db_offset);
}

bool Database::erase(const std::string& key) {
    auto exist = index_->find(key);

    Record record{RecordType::Delete, key, ""};
    std::ofstream file;

    if (exist) {
        try {
            file.open(db_path_, std::ios::binary | std::ios::app);
            file.exceptions(std::ios::failbit | std::ios::badbit);

            // Deletion records retain the key and serialize an empty value.
            write_record(file, record);
            file.flush();

        } catch (const std::ios_base::failure& error) {
            throw std::runtime_error{"Could not write database file: " + db_path_.string()};
        }

        // Unlink only after flushing the deletion record. If unlinking fails,
        // the index can still expose the old value until recovery is implemented.
        if (!index_->erase(key)) {
            throw std::runtime_error("Key " + key + " exists in db file but not in index file");
        }
    }

    return exist.has_value();
}

IndexStats Database::get_index_stats() const { return index_->stats(); }

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

    scan_records();
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

RecordScanResult Database::scan_records(std::uint64_t start_offset) {
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
    // for now: rebuild the index
    Record record{};
    RecordReadStatus status{};
    auto record_start = start_offset;
    auto record_end = file.tellg();
    std::uint64_t last_valid_offset = 0;
    std::uint64_t failing_record_offset = 0;
    while ((status = read_record(file, record)) == RecordReadStatus::Success) {
        last_valid_offset = record_start;
        record_end = file.tellg();
        // TODO: rebuild index here?
        record_start = record_end;
    }
    if (status != RecordReadStatus::EndOfFile) {
        failing_record_offset = record_start;

        std::cout << "db file has some problem, error code: " << static_cast<std::uint8_t>(status)
                  << '\n';

        // std::cout << "truncating the file...\n";

        // // Close the stream before resizing
        // file.close();

        // // Truncate the file at the recorded position
        // std::filesystem::resize_file(db_path_, record_end);
    } else {
        std::cout << "db file is healthy\n";
    }

    return RecordScanResult{status, last_valid_offset, failing_record_offset};
}

void Database::recover_records() {}

} // namespace zidanedb
