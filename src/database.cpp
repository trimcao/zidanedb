#include "zidanedb/database.h"
#include <cstdint>
#include <iostream>
#include <optional>
#include <unordered_map>
#include <utility>
#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <cppcodec/base64_rfc4648.hpp>
#include <string>

namespace {

void write_string(std::ofstream& file, const std::string& s)
{
    const std::uint32_t length = static_cast<std::uint32_t>(s.size());
    file.write(
        reinterpret_cast<const char*>(&length),
        sizeof(length)
    );
    file.write(
        s.data(),
        static_cast<std::streamsize>(s.size())
    );
}

bool read_string(std::ifstream& file, std::string& result)
{
    std::uint32_t length = {};
    if (!file.read(
            reinterpret_cast<char*>(&length),
            sizeof(length))) {
        return false;
    }

    // assume that length == 0 means the key is deleted
    if (length == 0) return false;

    result.resize(length);

    if (!file.read(
            result.data(),
            static_cast<std::streamsize>(length))) {
        return false;
    }

    return true;

}

}// namespace

namespace zidanedb {

Database::Database(std::filesystem::path path)
{
    db_path_ = std::move(path);
    index_ = std::unordered_map<std::string, uint64_t>{};

    // check the path, if it exists, load the data to the map
    if (!std::filesystem::exists(db_path_)) {
        return;
    }

    // remember: After std::move(x), don’t read the old value of x;
    // destroy it or assign a new value to it.
    // So don't use `path` here, use `db_path_`
    std::ifstream file{
        db_path_,
        std::ios::binary
    };
    if (!file) {
        std::cerr << "Could not open the file\n";
        return;
    }

    std::string key, value;
    uint64_t offset;
    while (read_string(file, key)) {
        // save the val offset to the index
        offset = file.tellg();
        // TODO: check tellg error?
        index_[key] = offset;
        if (!read_string(file, value)) {
            std::cerr << "Incomplete database record\n";
            break;
        }
    }
}

std::optional<std::string>
Database::get(const std::string& key) const
{
    std::string val;

    auto offset = index_.find(key);
    if (offset == index_.end() || offset->second == 0) {
        return std::nullopt;
    }

    // read the value from the db file
    std::ifstream file{
        db_path_,
        std::ios::binary
    };
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
    if(!read_string(file, val)) {
        return std::nullopt;
    }

    return val;
}

void Database::put(std::string key, std::string val)
{
    // assume that we will keep appending even if new_val == current_val

    std::ofstream file;

    // new approach: keep writing to the db file
    try {
        file.open(
            db_path_,
            std::ios::binary | std::ios::app
        );

        // assumption: the last write wins,
        // the last value of the key stays
        write_string(file, key);
        index_.insert_or_assign(key, file.tellp());
        write_string(file, val);

        file.close();
    } catch (const std::ios_base::failure &error) {
        throw std::runtime_error {
            "Could not write database file: " + db_path_.string()
        };
    }
}

bool Database::erase(const std::string& key)
{
    bool retval = index_.erase(key);
    // new approach:
    // keep writing to the db file

    std::ofstream file;

    // only update the db file if the key is deleted
    if (retval) {
        try {
            file.open(
                db_path_,
                std::ios::binary | std::ios::app
            );

            // assumption: empty value means the key is deleted
            write_string(file, key);
            write_string(file, "");

            file.close();
        } catch (const std::ios_base::failure &error) {
            throw std::runtime_error {
                "Could not write database file: " + db_path_.string()
            };
        }
    }

    return retval;
}

}
