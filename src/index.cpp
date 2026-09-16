#include "index.h"
#include "constants.h"
#include "utils.h"
#include "zidanedb/index_stats.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
namespace zidanedb {

/*
strategy: use a persistent hash map

The header of the index file is:
[magic][version][num_buckets]

The format of the index entry is:
[db_offset][next_entry][key_length][key_bytes]
Note: currently in the code, [key_length][key_bytes] is handled together
in write_string() and read_string() methods.

*/

EntryLocation Index::find_entry_offset(std::istream& file, const std::string& key) const {
    std::uint64_t bucket = utils::fnv1a(key) % num_buckets_;
    std::uint64_t bucket_offset = header_size() + bucket * sizeof(std::uint64_t);

    try {
        std::uint64_t idx_chain_offset = 0;          // start of the colission chain
        std::uint64_t existing_idx_entry_offset = 0; // the entry offset for the given key
        std::uint64_t prev_entry_offset = 0;         // prev entry of the existing entry (if found)
        IndexEntryHeader read_entry_header;

        file.seekg(bucket_offset);
        if (!file) {
            throw std::runtime_error{"seek failed"};
        }
        if (!utils::read_uint64(file, idx_chain_offset)) {
            throw std::runtime_error{"could not read chain_offset from bucket"};
        }

        // find the entry if it exists
        std::string k;
        std::uint64_t cur_entry_offset;

        // assumption: existing_idx_entry_offset == 0 means could not find the key
        if (!idx_chain_offset) {
            // no key found
            existing_idx_entry_offset = 0;
        } else {
            cur_entry_offset = idx_chain_offset;
            while (cur_entry_offset) {
                file.seekg(cur_entry_offset);
                if (!file) {
                    throw std::runtime_error{"seek failed"};
                }
                if (!utils::read_uint64(file, read_entry_header.db_offset)) {
                    throw std::runtime_error{"could not read db offset"};
                }
                if (!utils::read_uint64(file, read_entry_header.next_entry_offset)) {
                    throw std::runtime_error{"could not read next entry offset"};
                }
                if (!utils::read_string(file, k)) {
                    throw std::runtime_error{"could not read key"};
                }
                if (key == k) {
                    existing_idx_entry_offset = cur_entry_offset;
                    break;
                }
                prev_entry_offset = cur_entry_offset;
                cur_entry_offset = read_entry_header.next_entry_offset;
            }
        }

        if (existing_idx_entry_offset == 0)
            prev_entry_offset = 0;

        return EntryLocation{existing_idx_entry_offset, prev_entry_offset, bucket_offset,
                             idx_chain_offset};
    } catch (const std::ios_base::failure& error) {
        throw std::runtime_error{"Could not read index file: " + path_.string()};
    }
}

Index::Index(std::filesystem::path path, std::uint64_t num_buckets) {
    if (num_buckets == 0) {
        throw std::runtime_error("Number of buckets must be a positive number");
    }

    path_ = std::move(path);
    num_buckets_ = num_buckets;
    if (std::filesystem::exists(path_)) {
        load();
    } else {
        setup();
    }
}

std::optional<std::uint64_t> Index::find(const std::string& key) const {
    std::fstream file{path_, std::ios::in | std::ios::binary};
    if (!file) {
        throw std::runtime_error{"Could not open index file: " + path_.string()};
    }

    auto offsets = find_entry_offset(file, key);
    if (offsets.entry_offset == 0) {
        return std::nullopt;
    }

    try {
        IndexEntryHeader read_entry_header;

        file.seekg(offsets.entry_offset);
        if (!file) {
            throw std::runtime_error{"seek failed"};
        }
        if (!utils::read_uint64(file, read_entry_header.db_offset)) {
            throw std::runtime_error{"cannot read db offset"};
        }
        return read_entry_header.db_offset;

    } catch (const std::ios_base::failure& error) {
        throw std::runtime_error{"Could not read index file: " + path_.string()};
    }
}

void Index::set(std::string key, std::uint64_t db_offset) {
    std::fstream file{path_, std::ios::in | std::ios::out | std::ios::binary};
    if (!file) {
        throw std::runtime_error{"Could not open index file: " + path_.string()};
    }

    auto offsets = find_entry_offset(file, key);

    try {
        std::uint64_t next_entry_offset = 0;

        // write new index entry
        file.exceptions(std::ios::failbit | std::ios::badbit);
        if (offsets.entry_offset) {
            // overwrite an existing entry
            // we will only overwrite the db_offset part
            file.seekp(offsets.entry_offset);
            if (!file) {
                throw std::runtime_error{"seek failed"};
            }
            utils::write_uint64(file, db_offset);
        } else {
            // create a new entry, and insert at the head
            next_entry_offset = offsets.chain_offset;
            file.seekp(0, std::ios::end);
            if (!file) {
                throw std::runtime_error{"seek failed"};
            }
            offsets.chain_offset = file.tellp(); // get the idx_chain_offset for this key

            utils::write_uint64(file, db_offset);
            utils::write_uint64(file, next_entry_offset);
            utils::write_string(file, key);
            file.flush();
            file.clear();

            // update the idx_chain_offset (the head offset, read from the bucket)
            file.seekp(offsets.bucket_offset);
            if (!file) {
                throw std::runtime_error{"seek failed"};
            }
            utils::write_uint64(file, offsets.chain_offset);
        }
        file.flush();

    } catch (const std::ios_base::failure& error) {
        throw std::runtime_error{"Could not update index file: " + path_.string()};
    }
}

bool Index::erase(const std::string& key) {
    std::fstream file{path_, std::ios::in | std::ios::out | std::ios::binary};
    if (!file) {
        throw std::runtime_error{"Could not open index file: " + path_.string()};
    }

    EntryLocation offsets = find_entry_offset(file, key);

    try {
        std::uint64_t next_entry_offset = 0;

        // remove index entry from the linked list
        // note: garbage will be left behind
        file.exceptions(std::ios::failbit | std::ios::badbit);
        if (offsets.entry_offset == 0) {
            return false;
        } else {
            // make the db_offset field null so it's easier to check
            file.seekp(offsets.entry_offset);
            if (!file) {
                throw std::runtime_error{"seek failed"};
            }
            utils::write_uint64(file, 0);

            // delete scenarios: delete at the head, delete in the middle
            file.seekg(offsets.entry_offset + sizeof(std::uint64_t));
            if (!file) {
                throw std::runtime_error{"seek failed"};
            }
            utils::read_uint64(file, next_entry_offset);

            if (offsets.prev_entry_offset) {
                // delete in the middle
                // update the next entry offset for the prev_entry
                file.seekp(offsets.prev_entry_offset + sizeof(std::uint64_t));
                if (!file) {
                    throw std::runtime_error{"seek failed"};
                }
                utils::write_uint64(file, next_entry_offset);
            } else {
                // delete the head
                // update the idx_chain_offset (the head offset, read from the bucket)
                file.seekp(offsets.bucket_offset);
                if (!file) {
                    throw std::runtime_error{"seek failed"};
                }
                utils::write_uint64(file, next_entry_offset);
            }
            file.flush();
            return true;
        }

    } catch (const std::ios_base::failure& error) {
        throw std::runtime_error{"Could not update index file: " + path_.string()};
    }
}

void Index::load() {
    if (!std::filesystem::exists(path_)) {
        throw std::runtime_error("Index file does not exist");
    }

    std::ifstream file{path_, std::ios::binary};
    if (!file) {
        throw std::runtime_error("Could not open index file: " + path_.string());
    }

    if (!utils::read_string(file, magic_) || (magic_ != INDEX_MAGIC)) {
        throw std::runtime_error("Invalid ZidaneDB Index file");
    }
    if (!utils::read_uint32(file, version_) || (version_ != INDEX_VERSION)) {
        throw std::runtime_error("Unsupported ZidaneDB Index version");
    }
    if (!utils::read_uint64(file, num_buckets_) || (num_buckets_ == 0)) {
        throw std::runtime_error("Invalid ZidaneDB Index number of buckets");
    }

    // check file size
    if (num_buckets_ >
        (std::filesystem::file_size(path_) - header_size()) / sizeof(std::uint64_t)) {
        throw std::runtime_error("ZidaneDB Index file size is smaller than required");
    }
}

void Index::setup() {
    if (std::filesystem::exists(path_)) {
        return;
    }

    // hardcode some values here, will make it more formal later
    magic_ = INDEX_MAGIC;
    version_ = INDEX_VERSION;

    try {
        std::ofstream file{path_, std::ios::binary};
        if (!file) {
            throw std::runtime_error("Could not open the file " + path_.string());
        }
        file.exceptions(std::ios::failbit | std::ios::badbit);

        utils::write_string(file, magic_);
        utils::write_uint32(file, version_);
        utils::write_uint64(file, num_buckets_);

        // initialize the buckets
        std::uint64_t empty = 0;
        for (std::uint64_t i = 0; i < num_buckets_; ++i) {
            utils::write_uint64(file, empty);
        }

        file.flush();
    } catch (const std::ios_base::failure& error) {
        throw std::runtime_error{"Could not update index file: " + path_.string()};
    }
}

std::uint64_t Index::index_size_before_entries() const {
    // magic + version + bucket_count + bucket_bytes
    return utils::string_size(magic_) + sizeof(std::uint32_t) + sizeof(std::uint64_t) +
           num_buckets_ * sizeof(std::uint64_t);
}

std::uint64_t Index::header_size() const {
    // magic + version + bucket_count
    return utils::string_size(magic_) + sizeof(std::uint32_t) + sizeof(std::uint64_t);
}

IndexStats Index::stats() const {
    IndexStats result;
    result.num_buckets = num_buckets_;
    result.empty_buckets = 0;
    result.non_empty_buckets = 0;
    result.avg_chain_length = 0.0;
    result.max_chain_length = 0;

    std::uint64_t total_length = 0;

    try {
        std::ifstream file{path_, std::ios::binary};
        if (!file) {
            throw std::runtime_error{"Could not open index file: " + path_.string()};
        }

        std::uint64_t idx_chain_offset = 0; // start of the colission chain
        IndexEntryHeader read_entry_header;

        std::uint64_t bucket_offset;
        for (std::uint64_t i = 0; i < num_buckets_; i++) {
            bucket_offset = header_size() + i * sizeof(std::uint64_t);
            file.seekg(bucket_offset);
            if (!file) {
                throw std::runtime_error{"seek failed"};
            }
            if (!utils::read_uint64(file, idx_chain_offset)) {
                throw std::runtime_error{"could not read chain_offset from bucket"};
            }

            // go through the chain
            std::uint64_t chain_length = 0;
            std::uint64_t cur_entry_offset;
            std::string k;
            if (!idx_chain_offset) {
                // no key found
                result.empty_buckets++;
            } else {
                result.non_empty_buckets++;
                cur_entry_offset = idx_chain_offset;
                while (cur_entry_offset) {
                    file.seekg(cur_entry_offset);
                    if (!file) {
                        throw std::runtime_error{"seek failed"};
                    }
                    if (!utils::read_uint64(file, read_entry_header.db_offset)) {
                        throw std::runtime_error{"could not read db offset"};
                    }
                    if (!utils::read_uint64(file, read_entry_header.next_entry_offset)) {
                        throw std::runtime_error{"could not read next entry offset"};
                    }
                    if (!utils::read_string(file, k)) {
                        throw std::runtime_error{"could not read key"};
                    }
                    chain_length++;
                    cur_entry_offset = read_entry_header.next_entry_offset;
                }
            }
            total_length += chain_length;
            if (result.max_chain_length < chain_length) {
                result.max_chain_length = chain_length;
            }
        }

    } catch (const std::ios_base::failure& error) {
        throw std::runtime_error{"Could not read index file: " + path_.string()};
    }

    // if non_empty_buckets == 0, avg_chain_length = 0
    if (result.non_empty_buckets > 0) {
        result.avg_chain_length = (double)total_length / result.non_empty_buckets;
    }

    return result;
}

} // namespace zidanedb