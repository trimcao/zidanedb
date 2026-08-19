#include "zidanedb/database.h"
#include <iostream>
#include <unordered_map>
#include <utility>
#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include "utils.h"

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
            data_[key] = decode_base64(value);
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
    std::ofstream file;
    // note about std::move():
    // std::move() gives permission to transfer resources from an object because
    // its current value is no longer needed.
    // std::move() itself does not perform the transfer. It marks the object as movable;
    // the receiving constructor or function decides what happens.
    data_.insert_or_assign(std::move(key), std::move(val));

    // one lesson: after doing std::move(key), the variable key does not contain
    // any data anymore.

    // idea: cannot appending to the db file mindlessly
    // duplicate keys cannot stay in the db.
    // very naive solution: write everything from scratch
    try {
        file.open(
            path_,
            std::ios::trunc
        );

        for (const auto& [k, v] : data_) {
            file << k << "\n";
            file << encode_base64(v) << "\n";
        }

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
    // very dumb approach:
    // delete the key-value pair from the map, and rewrite the whole thing.

    std::ofstream file;

    try {
        file.open(
            path_,
            std::ios::trunc
        );

        for (const auto& [k, v] : data_) {
            if (k != key) {
                file << k << "\n";
                file << encode_base64(v) << "\n";
            }
        }

        file.close();
    } catch (const std::ios_base::failure &error) {
        throw std::runtime_error {
            "Could not write database file: " + path_.string()
        };
    }

    return retval;
}

}
