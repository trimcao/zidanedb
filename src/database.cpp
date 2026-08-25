#include "zidanedb/database.h"
#include <iostream>
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
    path_ = std::move(path);
    data_ = std::unordered_map<std::string, std::string>{};

    // check the path, if it exists, load the data to the map
    if (!std::filesystem::exists(path_)) {
        return;
    }

    // remember: After std::move(x), don’t read the old value of x;
    // destroy it or assign a new value to it.
    // So don't use `path` here, use `path_`
    std::ifstream file{
        path_,
        std::ios::binary
    };
    if (!file) {
        std::cerr << "Could not open the file\n";
        return;
    }

    std::string key, value;
    while (read_string(file, key)) {
        if (!read_string(file, value)) {
            std::cerr << "Incomplete database record\n";
            break;
        }
        if (!value.empty())
            data_[key] = value;
        else
            data_.erase(key);
    }
}

std::optional<std::string>
Database::get(const std::string& key) const
{
    auto it = data_.find(key);
    if (it == data_.end()) {
        return std::nullopt;
    }

    return it->second;
}

void Database::put(std::string key, std::string val)
{
    // check if we actually need to put
    auto cur_val = get(key);
    if (cur_val == val) {
        return;
    }

    // note about std::move():
    // std::move() gives permission to transfer resources from an object because
    // its current value is no longer needed.
    // std::move() itself does not perform the transfer. It marks the object as movable;
    // the receiving constructor or function decides what happens.
    std::string k = key;
    std::string v = val;
    data_.insert_or_assign(std::move(key), std::move(val));

    // one lesson: after doing std::move(key), the variable key does not contain
    // any data anymore.

    std::ofstream file;

    // new approach: keep writing to the db file
    try {
        file.open(
            path_,
            std::ios::binary | std::ios::app
        );

        // assumption: the last write wins,
        // the last value of the key stays
        write_string(file, k);
        write_string(file, v);

        file.close();
    } catch (const std::ios_base::failure &error) {
        throw std::runtime_error {
            "Could not write database file: " + path_.string()
        };
    }
}

bool Database::erase(const std::string& key)
{
    bool retval = data_.erase(key);
    // new approach:
    // keep writing to the db file

    std::ofstream file;

    // only update the db file if the key is deleted
    if (retval) {
        try {
            file.open(
                path_,
                std::ios::binary | std::ios::app
            );

            // assumption: empty value means the key is deleted
            write_string(file, key);
            write_string(file, "");

            file.close();
        } catch (const std::ios_base::failure &error) {
            throw std::runtime_error {
                "Could not write database file: " + path_.string()
            };
        }
    }

    return retval;
}

}
