#include "zidanedb/database.h"
#include "constants.h"
#include "index.h"
#include "record.h"
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

    if (!read_record(file, record)) {
        throw std::runtime_error("Cannot get the db record");
    };

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

    // TODO: compute the checksum properly
    Record record{RecordType::Put, key, val, 0};
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

    Record record{RecordType::Delete, key, "", 0};
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

} // namespace zidanedb
