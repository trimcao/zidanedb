#include "index.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <optional>
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

bool read_uint32(std::istream& stream, std::uint32_t& result) {
    if (!stream.read(reinterpret_cast<char*>(&result), sizeof(std::uint32_t))) {
        return false;
    }

    return true;
}

void write_uint32(std::ostream& stream, const std::uint32_t n) {
    stream.write(reinterpret_cast<const char*>(&n), sizeof(n));
}

std::uint64_t fnv1a(std::string_view key) {
    std::uint64_t hash = 14695981039346656037ULL;

    for (unsigned char c : key) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }

    return hash;
}

// return the number of bytes used by write_string() function above
std::uint32_t string_size(std::string_view s) { return sizeof(std::uint32_t) + s.size(); }

} // namespace

namespace zidanedb {

Index::Index(std::filesystem::path path) {
    path_ = std::move(path);
    if (std::filesystem::exists(path_)) {
        load();
    } else {
        setup();
    }
}

std::optional<std::uint64_t> Index::find(const std::string& key) const {
    std::uint64_t bucket = fnv1a(key) % num_buckets_;
    std::uint64_t header_size = string_size(magic_) + sizeof(std::uint32_t) + sizeof(std::uint64_t);
    std::uint64_t bucket_offset = header_size + bucket * sizeof(std::uint64_t);

    // std::cout << "header_size: " << header_size << '\n';
    // std::cout << "num_buckets_: " << num_buckets_ << '\n';
    // std::cout << "bucket num: " << bucket << '\n';

    try {
        std::fstream file{path_, std::ios::in | std::ios::binary};
        if (!file) {
            throw std::runtime_error{"Could not open index file: " + path_.string()};
        }

        // read the current idx offset to find an existing entry
        std::uint64_t idx_chain_offset = 0;          // start of the colission chain
        std::uint64_t existing_idx_entry_offset = 0; // the entry offset for the given key
        IndexEntryHeader read_entry_header;

        file.seekg(bucket_offset);
        read_uint64(file, idx_chain_offset);

        // std::cout << "idx_chain_offset: " << idx_chain_offset << '\n';

        // find the entry if it exists
        std::string k;
        std::uint64_t cur_entry_offset;

        if (!idx_chain_offset) {
            return std::nullopt;
        } else {
            cur_entry_offset = idx_chain_offset;
            while (cur_entry_offset) {
                // std::cout << "cur_entry_offset: " << cur_entry_offset << '\n';
                file.seekg(cur_entry_offset);
                // TODO: error handling for the following reads
                read_uint32(file, read_entry_header.key_length);
                read_uint64(file, read_entry_header.db_offset);
                read_uint64(file, read_entry_header.next_entry_offset);
                read_string(file, k);
                if (key == k) {
                    existing_idx_entry_offset = cur_entry_offset;
                    break;
                }
                cur_entry_offset = read_entry_header.next_entry_offset;
            }
        }

        if (existing_idx_entry_offset) {
            return read_entry_header.db_offset;
        } else {
            return std::nullopt;
        }

    } catch (const std::ios_base::failure& error) {
        throw std::runtime_error{"Could not read index file: " + path_.string()};
    }
}

void Index::set(std::string key, std::uint64_t db_offset) {
    // offsets_.insert_or_assign(key, db_offset);

    // strategy: use a persistent hash map

    std::uint64_t bucket = fnv1a(key) % num_buckets_;

    std::uint64_t header_size = string_size(magic_) + sizeof(std::uint32_t) + sizeof(std::uint64_t);
    std::uint64_t bucket_offset = header_size + bucket * sizeof(std::uint64_t);

    try {
        std::fstream file{path_, std::ios::in | std::ios::out | std::ios::binary};
        if (!file) {
            throw std::runtime_error{"Could not open index file: " + path_.string()};
        }

        std::uint64_t next_entry_offset = 0;
        // read the current idx offset to find an existing entry
        std::uint64_t idx_chain_offset = 0;          // start of the colission chain
        std::uint64_t existing_idx_entry_offset = 0; // the entry offset for the given key
        IndexEntryHeader read_entry_header;

        file.seekg(bucket_offset);
        read_uint64(file, idx_chain_offset);

        // find the entry if it exists
        std::string k;
        std::uint64_t cur_entry_offset;

        if (!idx_chain_offset) {
            existing_idx_entry_offset = 0;
        } else {
            cur_entry_offset = idx_chain_offset;
            while (cur_entry_offset) {
                file.seekg(cur_entry_offset);
                // TODO: error handling for the following reads
                read_uint32(file, read_entry_header.key_length);
                read_uint64(file, read_entry_header.db_offset);
                read_uint64(file, read_entry_header.next_entry_offset);
                read_string(file, k);
                if (key == k) {
                    existing_idx_entry_offset = cur_entry_offset;
                    break;
                }
                cur_entry_offset = read_entry_header.next_entry_offset;
            }
        }
        file.clear();

        // write new index entry
        file.exceptions(std::ios::failbit | std::ios::badbit);
        if (existing_idx_entry_offset) {
            // overwrite an existing entry
            // we will only overwrite the db_offset part
            // need to get past the key_length field
            file.seekp(existing_idx_entry_offset + sizeof(std::uint32_t));
            write_uint64(file, db_offset);
        } else {
            // create a new entry, and insert at the head
            next_entry_offset = idx_chain_offset;
            file.seekp(0, std::ios::end);
            idx_chain_offset = file.tellp(); // get the idx_chain_offset for this key

            write_uint32(file, key.size());
            write_uint64(file, db_offset);
            write_uint64(file, next_entry_offset);
            write_string(file, key);
            file.flush();
            file.clear();

            // update the idx_chain_offset (the head offset, read from the bucket)
            file.seekp(bucket_offset);
            write_uint64(file, idx_chain_offset);
        }
        file.flush();

    } catch (const std::ios_base::failure& error) {
        throw std::runtime_error{"Could not update index file: " + path_.string()};
    }
}

bool Index::erase(const std::string& key) {
    // bool retval = offsets_.erase(key);

    bool retval = false;

    std::uint64_t bucket = fnv1a(key) % num_buckets_;
    std::uint64_t header_size = string_size(magic_) + sizeof(std::uint32_t) + sizeof(std::uint64_t);
    std::uint64_t bucket_offset = header_size + bucket * sizeof(std::uint64_t);

    try {
        std::fstream file{path_, std::ios::in | std::ios::out | std::ios::binary};
        if (!file) {
            throw std::runtime_error{"Could not open index file: " + path_.string()};
        }

        std::uint64_t prev_entry_offset = 0;
        std::uint64_t next_entry_offset = 0;
        // read the current idx offset to find an existing entry
        std::uint64_t idx_chain_offset = 0;          // start of the colission chain
        std::uint64_t existing_idx_entry_offset = 0; // the entry offset for the given key
        IndexEntryHeader read_entry_header;

        file.seekg(bucket_offset);
        read_uint64(file, idx_chain_offset);

        // find the entry if it exists
        std::string k;
        std::uint64_t cur_entry_offset;

        // TODO: show some error if could not find an existing key here
        // (or decide if I actually need it)

        if (!idx_chain_offset) {
            // no key found
            existing_idx_entry_offset = 0;
        } else {
            cur_entry_offset = idx_chain_offset;
            while (cur_entry_offset) {
                file.seekg(cur_entry_offset);
                // TODO: error handling for the following reads
                read_uint32(file, read_entry_header.key_length);
                read_uint64(file, read_entry_header.db_offset);
                read_uint64(file, read_entry_header.next_entry_offset);
                read_string(file, k);
                if (key == k) {
                    existing_idx_entry_offset = cur_entry_offset;
                    break;
                }
                prev_entry_offset = cur_entry_offset;
                cur_entry_offset = read_entry_header.next_entry_offset;
            }
        }
        file.clear();

        // remove index entry from the linked list
        // note: garbage will be left behind
        file.exceptions(std::ios::failbit | std::ios::badbit);
        if (existing_idx_entry_offset) {
            // make the db_offset field null so it's easier to check
            file.seekp(existing_idx_entry_offset + sizeof(std::uint32_t));
            write_uint64(file, 0);
            // delete scenarios: delete at the head, delete in the middle
            next_entry_offset = read_entry_header.next_entry_offset;
            if (prev_entry_offset) {
                // delete in the middle
                // update the next entry offset for the prev_entry
                file.seekp(prev_entry_offset + sizeof(std::uint32_t) + sizeof(std::uint64_t));
                write_uint64(file, next_entry_offset);
            } else {
                // delete the head
                // update the idx_chain_offset (the head offset, read from the bucket)
                file.seekp(bucket_offset);
                write_uint64(file, next_entry_offset);
            }

            retval = true;
        }
        file.flush();

    } catch (const std::ios_base::failure& error) {
        throw std::runtime_error{"Could not update index file: " + path_.string()};
    }

    return retval;
}

void Index::load() {
    if (!std::filesystem::exists(path_)) {
        return;
    }

    std::ifstream file{path_, std::ios::binary};
    if (!file) {
        std::cerr << "Could not open the file " << path_.string() << '\n';
        return;
    }

    if (!read_string(file, magic_)) {
        std::cerr << "This is not a ZidaneDB Index file\n";
        return;
    }
    if (!read_uint32(file, version_)) {
        std::cerr << "Cannot read version of the Index file\n";
        return;
    }
    if (!read_uint64(file, num_buckets_)) {
        std::cerr << "Cannot read number of buckets in the Index file\n";
        return;
    }
}

void Index::setup() {
    if (std::filesystem::exists(path_)) {
        return;
    }

    // hardcode some values here, will make it more formal later
    magic_ = "ZIDANEDBINDEX026";
    version_ = 1;
    num_buckets_ = 1'000'000;

    std::ofstream file{path_, std::ios::binary};
    if (!file) {
        std::cerr << "Could not open the file " << path_.string() << '\n';
        return;
    }

    write_string(file, magic_);
    write_uint32(file, version_);
    write_uint64(file, num_buckets_);

    // initialize the buckets
    std::uint64_t empty = 0;
    for (std::uint64_t i = 0; i < num_buckets_; ++i) {
        write_uint64(file, empty);
    }

    file.flush();
}

std::uint64_t Index::get_start_entry_offset() {
    // magic + version + bucket_count + bucket_bytes
    return string_size(magic_) + sizeof(std::uint32_t) + sizeof(std::uint64_t) +
           num_buckets_ * sizeof(std::uint64_t);
}

} // namespace zidanedb