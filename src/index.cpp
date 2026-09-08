#include "index.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <string>
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

Index::Index(std::filesystem::path path) {
    path_ = std::move(path);
    load();
}

std::optional<std::uint64_t> Index::find(const std::string& key) const {
    auto offset = offsets_.find(key);
    if (offset == offsets_.end() || offset->second == 0) {
        return std::nullopt;
    }
    return offset->second;
}

void Index::set(std::string key, std::uint64_t db_offset) {
    // create the idx file if it does not exist
    if (!std::filesystem::exists(path_)) {
        std::ofstream create(path_, std::ios::binary);
    }

    offsets_.insert_or_assign(key, db_offset);

    // strategy:
    // - find the index of the given key in the index file.
    // - if not found, append the a new index entry.
    // - if found, edit the offset value for that key.
    std::uint64_t idx_offset = UINT64_MAX;
    try {
        std::fstream idxfile{path_, std::ios::in | std::ios::out | std::ios::binary};
        if (!idxfile) {
            throw std::runtime_error{"Could not open index file: " + path_.string()};
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
        throw std::runtime_error{"Could not update index file: " + path_.string()};
    }
}

bool Index::erase(const std::string& key) {
    bool retval = offsets_.erase(key);

    if (retval) {
        std::uint64_t idx_offset = UINT64_MAX;
        try {
            std::fstream idxfile{path_, std::ios::in | std::ios::out | std::ios::binary};
            if (!idxfile) {
                throw std::runtime_error{"Could not open index file: " + path_.string()};
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
            throw std::runtime_error{"Could not update index file: " + path_.string()};
        }
    }
    return retval;
}

void Index::load() {

    // Note: we load the index file now, not the db file
    // check if the file exists
    if (!std::filesystem::exists(path_)) {
        return;
    }

    std::ifstream file{path_, std::ios::binary};
    if (!file) {
        std::cerr << "Could not open the file " << path_.string() << '\n';
        return;
    }

    std::string key;
    std::uint64_t offset;
    while (read_string(file, key)) {
        if (!read_uint64(file, offset)) {
            std::cerr << "Incomplete database record\n";
            break;
        }
        offsets_[key] = offset;
    }
}

} // namespace zidanedb