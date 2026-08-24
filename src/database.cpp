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

using base64 = cppcodec::base64_rfc4648;

inline std::string encode_base64(const std::string& input)
{
    return base64::encode(input);
}

inline std::string decode_base64(const std::string& input)
{
    return base64::decode<std::string>(input);
}

} // namespace

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
    std::ifstream file{path_};
    if (!file) {
        std::cerr << "Could not open the file\n";
        return;
    }

    int line_num = 0;
    std::string line;
    std::string key, value;
    while (std::getline(file, line)) {
        if (line_num % 2 == 0) {
            key = line;
        } else {
            value = line;
            if (!value.empty())
                data_[key] = decode_base64(value);
            else
                data_.erase(key);
        }
        line_num++;
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
            std::ios::app
        );

        // assumption: the last write wins,
        // the last value of the key stays
        file << k << "\n";
        file << encode_base64(v) << "\n";

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
                std::ios::app
            );

            // assumption: empty value means the key is deleted
            file << key << "\n";
            file << "" << "\n";

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
