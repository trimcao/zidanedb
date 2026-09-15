#include "zidanedb/database.h"
#include "index.h"
#include "utils.h"
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

enum class RecordType : std::uint8_t { Put = 1, Delete = 2 };

Database::Database(std::filesystem::path path) {
    db_path_ = std::move(path);
    idx_path_ = db_path_;
    idx_path_.replace_extension(".zidx");

    index_ = std::make_unique<Index>(idx_path_);
}

Database::Database(std::filesystem::path path, std::uint64_t num_index_buckets) {
    db_path_ = std::move(path);
    idx_path_ = db_path_;
    idx_path_.replace_extension(".zidx");

    index_ = std::make_unique<Index>(idx_path_, num_index_buckets);
}

Database::~Database() = default;

std::optional<std::string> Database::get(const std::string& key) const {
    std::string val;

    // assumption: to differentiate between existing key with empty value and
    // deleted key, for now, we can rely on the index.
    // Later, we can read the record type if needed.
    auto offset = index_->find(key);
    if (!offset) {
        return std::nullopt;
    }

    // read the value from the db file
    std::ifstream file{db_path_, std::ios::binary};
    if (!file) {
        std::cerr << "Could not open the file\n";
        return std::nullopt;
    }
    file.seekg(*offset, std::ios::beg);
    if (!file) {
        std::cerr << "Seek failed\n";
        return std::nullopt;
    }

    // try to read the val
    if (!utils::read_string(file, val)) {
        return std::nullopt;
    }

    return val;
}

void Database::put(std::string key, std::string val) {
    // assume that we will keep appending even if new_val == current_val

    std::ofstream file;
    std::uint64_t db_offset;

    // new approach: keep writing to the db file
    try {
        file.open(db_path_, std::ios::binary | std::ios::app);
        file.exceptions(std::ios::failbit | std::ios::badbit);

        // assumption: the last write wins,
        // the last value of the key stays

        // write the record type
        utils::write_uint8(file, static_cast<std::uint8_t>(RecordType::Put));

        utils::write_string(file, key);
        const auto position = file.tellp();
        if (position == std::ostream::pos_type(-1)) {
            throw std::runtime_error{"tellp() failed"};
        }

        db_offset = static_cast<std::uint64_t>(static_cast<std::streamoff>(position));
        utils::write_string(file, val);
        file.flush();
    } catch (const std::ios_base::failure& error) {
        throw std::runtime_error{"Could not write database file: " + db_path_.string()};
    }

    // update the index file
    index_->set(key, db_offset);
}

bool Database::erase(const std::string& key) {
    bool retval = index_->erase(key);

    // approach:
    // keep writing to the db file
    std::ofstream file;
    // only update the db file if the key is deleted
    if (retval) {
        try {
            file.open(db_path_, std::ios::binary | std::ios::app);
            file.exceptions(std::ios::failbit | std::ios::badbit);

            // record type will determine this key is deleted
            utils::write_uint8(file, static_cast<std::uint8_t>(RecordType::Delete));
            utils::write_string(file, key);
            utils::write_string(file, "");
            file.flush();

        } catch (const std::ios_base::failure& error) {
            throw std::runtime_error{"Could not write database file: " + db_path_.string()};
        }
    }

    return retval;
}

IndexStats Database::get_index_stats() const { return index_->stats(); }

} // namespace zidanedb
