#include "zidanedb/database.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace {

void write_string(std::ostream& stream, const std::string& s) {
    const std::uint32_t length = static_cast<std::uint32_t>(s.size());
    stream.write(reinterpret_cast<const char*>(&length), sizeof(length));
    stream.write(s.data(), static_cast<std::streamsize>(s.size()));
}

bool read_string(std::istream& stream, std::string& result) {
    std::uint32_t length = {};
    if (!stream.read(reinterpret_cast<char*>(&length), sizeof(length))) {
        return false;
    }

    // assume that length == 0 means the key is deleted
    if (length == 0)
        return false;

    result.resize(length);

    if (!stream.read(result.data(), static_cast<std::streamsize>(length))) {
        return false;
    }

    return true;
}

bool read_uint64(std::istream& stream, std::uint64_t& result) {
    if (!stream.read(reinterpret_cast<char*>(&result), sizeof(std::uint64_t))) {
        return false;
    }

    return true;
}

void write_uint64(std::ostream& stream, const std::uint64_t n) {
    stream.write(reinterpret_cast<const char*>(&n), sizeof(n));
}

} // namespace

namespace zidanedb {

Database::Database(std::filesystem::path path) {
    db_path_ = std::move(path);
    idx_path_ = db_path_;
    idx_path_.replace_extension(".zidx");

    index_ = std::unordered_map<std::string, std::uint64_t>{};

    // Note: we load the index file now, not the db file
    // check if the file exists
    if (!std::filesystem::exists(idx_path_)) {
        return;
    }

    std::ifstream file{idx_path_, std::ios::binary};
    if (!file) {
        std::cerr << "Could not open the file " << idx_path_.string() << '\n';
        return;
    }

    std::string key;
    std::uint64_t offset;
    while (read_string(file, key)) {
        if (!read_uint64(file, offset)) {
            std::cerr << "Incomplete database record\n";
            break;
        }
        index_[key] = offset;
    }
}

std::optional<std::string> Database::get(const std::string& key) const {
    std::string val;

    auto offset = index_.find(key);
    if (offset == index_.end() || offset->second == 0) {
        return std::nullopt;
    }

    // read the value from the db file
    std::ifstream file{db_path_, std::ios::binary};
    if (!file) {
        std::cerr << "Could not open the file\n";
        return std::nullopt;
    }
    file.seekg(offset->second, std::ios::beg);
    if (!file) {
        std::cerr << "Seek failed\n";
        return std::nullopt;
    }

    // try to read the val
    if (!read_string(file, val)) {
        return std::nullopt;
    }

    return val;
}

void Database::put(std::string key, std::string val) {
    // TODO: decide what it means to have val equal to empty.

    // assume that we will keep appending even if new_val == current_val

    std::ofstream file;
    std::uint64_t db_offset;

    // new approach: keep writing to the db file
    try {
        file.open(db_path_, std::ios::binary | std::ios::app);
        file.exceptions(std::ios::failbit | std::ios::badbit);

        // assumption: the last write wins,
        // the last value of the key stays
        write_string(file, key);
        const auto position = file.tellp();
        if (position == std::ostream::pos_type(-1)) {
            throw std::runtime_error{"tellp() failed"};
        }

        db_offset = static_cast<std::uint64_t>(static_cast<std::streamoff>(position));
        write_string(file, val);
        file.flush();
    } catch (const std::ios_base::failure& error) {
        throw std::runtime_error{"Could not write database file: " + db_path_.string()};
    }

    // update the index file

    // create the idx file if it does not exist
    if (!std::filesystem::exists(idx_path_)) {
        std::ofstream create(idx_path_, std::ios::binary);
    }

    index_.insert_or_assign(key, db_offset);

    // strategy:
    // - find the index of the given key in the index file.
    // - if not found, append the a new index entry.
    // - if found, edit the offset value for that key.
    std::uint64_t idx_offset = UINT64_MAX;
    try {
        std::fstream idxfile{idx_path_, std::ios::in | std::ios::out | std::ios::binary};
        if (!idxfile) {
            throw std::runtime_error{"Could not open index file: " + idx_path_.string()};
        }

        std::string k;
        std::uint64_t offset;
        while (read_string(idxfile, k)) {
            if (key == k) {
                idx_offset = idxfile.tellg();
                break;
            }
            if (!read_uint64(idxfile, offset)) {
                std::cerr << "Incomplete index record\n";
                break;
            }
        }

        idxfile.clear();

        idxfile.exceptions(std::ios::failbit | std::ios::badbit);
        if (idx_offset == UINT64_MAX) {
            idxfile.seekp(0, std::ios::end);
            write_string(idxfile, key);
        } else {
            idxfile.seekp(idx_offset, std::ios::beg);
        }
        write_uint64(idxfile, db_offset);
        idxfile.flush();

    } catch (const std::ios_base::failure& error) {
        throw std::runtime_error{"Could not update index file: " + idx_path_.string()};
    }
}

bool Database::erase(const std::string& key) {
    bool retval = index_.erase(key);
    // new approach:
    // keep writing to the db file

    std::ofstream file;

    // only update the db file if the key is deleted
    if (retval) {
        try {
            file.open(db_path_, std::ios::binary | std::ios::app);
            file.exceptions(std::ios::failbit | std::ios::badbit);

            // assumption: empty value means the key is deleted
            write_string(file, key);
            write_string(file, "");
            file.flush();

        } catch (const std::ios_base::failure& error) {
            throw std::runtime_error{"Could not write database file: " + db_path_.string()};
        }

        // update index file
        std::uint64_t idx_offset = UINT64_MAX;

        try {
            std::fstream idxfile{idx_path_, std::ios::in | std::ios::out | std::ios::binary};
            if (!idxfile) {
                throw std::runtime_error{"Could not open index file: " + idx_path_.string()};
            }

            std::string k;
            std::uint64_t offset;
            while (read_string(idxfile, k)) {
                if (key == k) {
                    idx_offset = idxfile.tellg();
                    break;
                }
                if (!read_uint64(idxfile, offset)) {
                    std::cerr << "Incomplete index record\n";
                    break;
                }
            }

            idxfile.clear();

            idxfile.exceptions(std::ios::failbit | std::ios::badbit);
            if (idx_offset != UINT64_MAX) {
                idxfile.seekp(idx_offset, std::ios::beg);
                write_uint64(idxfile, 0);
            }
            idxfile.flush();

        } catch (const std::ios_base::failure& error) {
            throw std::runtime_error{"Could not update index file: " + idx_path_.string()};
        }
    }

    return retval;
}

} // namespace zidanedb
