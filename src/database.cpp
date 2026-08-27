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

bool read_uint64(std::ifstream& file, uint64_t& result)
{
    if (!file.read(
            reinterpret_cast<char*>(&result),
            sizeof(uint64_t))) {
        return false;
    }

    return true;
}

void write_uint64(std::ofstream& file, const uint64_t n)
{
    file.write(
        reinterpret_cast<const char*>(&n),
        sizeof(n)
    );
}

}// namespace

namespace zidanedb {

Database::Database(
    std::filesystem::path path,
    std::filesystem::path idx_path)
{
    db_path_ = std::move(path);
    idx_path_ = std::move(idx_path);
    index_ = std::unordered_map<std::string, uint64_t>{};

    // Note: we load the index file now, not the db file
    // check if the file exists
    if (!std::filesystem::exists(idx_path_)) {
        return;
    }

    std::ifstream file{
        idx_path_,
        std::ios::binary
    };
    if (!file) {
        std::cerr << "Could not open the file " << idx_path_.string() << '\n';
        return;
    }

    std::string key;
    uint64_t offset;
    while (read_string(file, key)) {
        if (!read_uint64(file, offset)) {
            std::cerr << "Incomplete database record\n";
            break;
        }
        index_[key] = offset;
    }
}

std::optional<std::string>
Database::get(const std::string& key) const
{
    // TODO: make sure the db file exists?

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
    // TODO: decide what it means to have val equal to empty.

    // assume that we will keep appending even if new_val == current_val

    std::ofstream file, idx_ofile;
    uint64_t db_offset;

    // new approach: keep writing to the db file
    try {
        file.open(
            db_path_,
            std::ios::binary | std::ios::app
        );

        // assumption: the last write wins,
        // the last value of the key stays
        write_string(file, key);
        db_offset = file.tellp();
        write_string(file, val);

    } catch (const std::ios_base::failure &error) {
        throw std::runtime_error {
            "Could not write database file: " + db_path_.string()
        };
    }

    if (!std::filesystem::exists(idx_path_)) {
        std::ofstream create(idx_path_, std::ios::binary);
    }

    std::ifstream idx_ifile;
    index_.insert_or_assign(key, db_offset);
    // update the index file
    uint64_t idx_offset = UINT64_MAX;
    try {
        // std::fstream idxfile{
        //     idx_path_,
        //     std::ios::in |
        //     std::ios::out |
        //     std::ios::binary
        // };

        idx_ifile.open(
            idx_path_,
            std::ios::binary
        );

        std::string k;
        uint64_t offset;
        while (read_string(idx_ifile, k)) {
            std::cout << "reading k=" << k << "\n";
            if (key == k) {
                idx_offset = idx_ifile.tellg();
                break;
            }
            if (!read_uint64(idx_ifile, offset)) {
                std::cerr << "Incomplete index record\n";
                break;
            }
        }

    } catch (const std::ios_base::failure &error) {
        throw std::runtime_error {
            "Could not read index file: " + idx_path_.string()
        };
    }

    try {
        idx_ofile.open(
            idx_path_,
            std::ios::binary | std::ios::in | std::ios::ate
        );

        std::cout << "idx_offset for " << key << ": " << idx_offset << "\n";

        if (idx_offset == UINT64_MAX) {
            std::cout << "equal max\n";

            idx_ofile.seekp(0, std::ios::end);
            write_string(idx_ofile, key);
        } else {
            std::cout << "not equal max\n";
            idx_ofile.seekp(idx_offset, std::ios::beg);
        }

        write_uint64(idx_ofile, db_offset);

    } catch (const std::ios_base::failure &error) {
        throw std::runtime_error {
            "Could not write index file: " + idx_path_.string()
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

        } catch (const std::ios_base::failure &error) {
            throw std::runtime_error {
                "Could not write database file: " + db_path_.string()
            };
        }

        // update index file
        std::ifstream idx_ifile;
        std::ofstream idx_ofile;
        uint64_t idx_offset = UINT64_MAX;

        try {
            idx_ifile.open(
                idx_path_,
                std::ios::binary
            );

            std::string k;
            uint64_t offset;
            while (read_string(idx_ifile, k)) {
                if (key == k) {
                    idx_offset = idx_ifile.tellg();
                    break;
                }
                if (!read_uint64(idx_ifile, offset)) {
                    std::cerr << "Incomplete index record\n";
                    break;
                }
            }

        } catch (const std::ios_base::failure &error) {
            throw std::runtime_error {
                "Could not read index file: " + idx_path_.string()
            };
        }

        try {
            idx_ofile.open(
                idx_path_,
                std::ios::binary | std::ios::in | std::ios::ate
            );

            if (idx_offset != UINT64_MAX) {
                idx_ofile.seekp(idx_offset, std::ios::beg);
                write_uint64(idx_ofile, 0);
            }

        } catch (const std::ios_base::failure &error) {
            throw std::runtime_error {
                "Could not write index file: " + idx_path_.string()
            };
        }
    }

    return retval;
}

}
