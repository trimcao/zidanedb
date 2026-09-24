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
Persistent hash index layout:
Header: [magic_length:u32][magic_bytes][version:u32][num_buckets:u64]
Buckets: [head_offset:u64] repeated num_buckets times
Entry: [db_offset:u64][next_entry:u64][key_length:u32][key_bytes]

db_offset points to the value's length prefix in the database file.
Bucket heads and next-entry offsets are absolute positions in the index file;
zero denotes an empty bucket or the end of a chain.
write_string() and read_string() handle each string's length prefix and bytes.
*/

EntryLocation Index::find_entry_offset(std::istream& file, const std::string& key) const {
    std::uint64_t bucket = utils::fnv1a(key) % num_buckets_;
    std::uint64_t bucket_offset = header_size() + bucket * sizeof(std::uint64_t);

    try {
        std::uint64_t idx_chain_offset = 0;
        std::uint64_t existing_idx_entry_offset = 0;
        std::uint64_t prev_entry_offset = 0;

        file.seekg(bucket_offset);
        if (!file) {
            throw std::runtime_error{"seek failed"};
        }
        if (utils::read_uint64(file, idx_chain_offset) != utils::ReadStatus::Success) {
            throw std::runtime_error{"could not read chain_offset from bucket"};
        }

        std::uint64_t cur_entry_offset;
        IndexEntry entry;
        if (!idx_chain_offset) {
            existing_idx_entry_offset = 0;
        } else {
            cur_entry_offset = idx_chain_offset;
            while (cur_entry_offset) {
                entry = read_entry(file, cur_entry_offset);
                if (key == entry.key) {
                    existing_idx_entry_offset = cur_entry_offset;
                    break;
                }
                prev_entry_offset = cur_entry_offset;
                cur_entry_offset = entry.next_entry_offset;
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
        if (utils::read_uint64(file, read_entry_header.db_offset) != utils::ReadStatus::Success) {
            throw std::runtime_error{"cannot read db offset"};
        }
        return read_entry_header.db_offset;

    } catch (const std::ios_base::failure& error) {
        throw std::runtime_error{"Could not read index file: " + path_.string()};
    }
}

void Index::set(const std::string& key, std::uint64_t db_offset) {
    if (key.size() > MAX_KEY_SIZE) {
        throw std::runtime_error("Key size exceeds max allowed key size");
    }

    std::fstream file{path_, std::ios::in | std::ios::out | std::ios::binary};
    if (!file) {
        throw std::runtime_error{"Could not open index file: " + path_.string()};
    }

    auto offsets = find_entry_offset(file, key);

    try {
        std::uint64_t next_entry_offset = 0;

        file.exceptions(std::ios::failbit | std::ios::badbit);
        if (offsets.entry_offset) {
            // Replacements update only db_offset; the key and chain links stay unchanged.
            file.seekp(offsets.entry_offset);
            if (!file) {
                throw std::runtime_error{"seek failed"};
            }
            utils::write_uint64(file, db_offset);
        } else {
            // Append the new entry and link it to the previous head.
            next_entry_offset = offsets.chain_offset;
            file.seekp(0, std::ios::end);
            if (!file) {
                throw std::runtime_error{"seek failed"};
            }
            offsets.chain_offset = file.tellp();

            utils::write_uint64(file, db_offset);
            utils::write_uint64(file, next_entry_offset);
            utils::write_string(file, key, MAX_KEY_SIZE);
            file.flush();
            file.clear();

            // Point the bucket at the new head only after flushing the entry.
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

        // Unlink entries without reclaiming their space in the index file.
        file.exceptions(std::ios::failbit | std::ios::badbit);
        if (offsets.entry_offset == 0) {
            return false;
        } else {
            // Mark the removed entry's database offset as zero.
            file.seekp(offsets.entry_offset);
            if (!file) {
                throw std::runtime_error{"seek failed"};
            }
            utils::write_uint64(file, 0);

            file.seekg(offsets.entry_offset + sizeof(std::uint64_t));
            if (!file) {
                throw std::runtime_error{"seek failed"};
            }
            utils::read_uint64(file, next_entry_offset);

            if (offsets.prev_entry_offset) {
                // Bypass the removed entry from its predecessor, including at the tail.
                file.seekp(offsets.prev_entry_offset + sizeof(std::uint64_t));
                if (!file) {
                    throw std::runtime_error{"seek failed"};
                }
                utils::write_uint64(file, next_entry_offset);
            } else {
                // Removing the head changes the bucket's entry offset.
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

    if ((utils::read_string(file, magic_, INDEX_MAGIC.size()) != utils::ReadStatus::Success) ||
        (magic_ != INDEX_MAGIC)) {
        throw std::runtime_error("Invalid ZidaneDB Index file");
    }
    if ((utils::read_uint32(file, version_) != utils::ReadStatus::Success) ||
        (version_ != INDEX_VERSION)) {
        throw std::runtime_error("Unsupported ZidaneDB Index version");
    }
    if ((utils::read_uint64(file, num_buckets_) != utils::ReadStatus::Success) ||
        (num_buckets_ == 0)) {
        throw std::runtime_error("Invalid ZidaneDB Index number of buckets");
    }

    // The declared bucket table must fit after the header.
    if (num_buckets_ >
        (std::filesystem::file_size(path_) - header_size()) / sizeof(std::uint64_t)) {
        throw std::runtime_error("ZidaneDB Index file size is smaller than required");
    }
}

void Index::setup() {
    if (std::filesystem::exists(path_)) {
        return;
    }

    magic_ = INDEX_MAGIC;
    version_ = INDEX_VERSION;

    try {
        std::ofstream file{path_, std::ios::binary};
        if (!file) {
            throw std::runtime_error("Could not open the file " + path_.string());
        }
        file.exceptions(std::ios::failbit | std::ios::badbit);

        utils::write_string(file, magic_, INDEX_MAGIC.size());
        utils::write_uint32(file, version_);
        utils::write_uint64(file, num_buckets_);

        // Zero bucket heads represent empty chains.
        std::uint64_t empty = 0;
        for (std::uint64_t i = 0; i < num_buckets_; ++i) {
            utils::write_uint64(file, empty);
        }

        file.flush();
    } catch (const std::ios_base::failure& error) {
        throw std::runtime_error{"Could not update index file: " + path_.string()};
    }
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

        std::uint64_t idx_chain_offset = 0;
        std::uint64_t bucket_offset;
        for (std::uint64_t i = 0; i < num_buckets_; i++) {
            bucket_offset = header_size() + i * sizeof(std::uint64_t);
            file.seekg(bucket_offset);
            if (!file) {
                throw std::runtime_error{"seek failed"};
            }
            if (utils::read_uint64(file, idx_chain_offset) != utils::ReadStatus::Success) {
                throw std::runtime_error{"could not read chain_offset from bucket"};
            }

            std::uint64_t chain_length = 0;
            std::uint64_t cur_entry_offset;
            IndexEntry entry;
            if (!idx_chain_offset) {
                result.empty_buckets++;
            } else {
                result.non_empty_buckets++;
                cur_entry_offset = idx_chain_offset;
                while (cur_entry_offset) {
                    entry = read_entry(file, cur_entry_offset);
                    chain_length++;
                    cur_entry_offset = entry.next_entry_offset;
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

    // Average over occupied buckets only; an empty index keeps the zero default.
    if (result.non_empty_buckets > 0) {
        result.avg_chain_length = (double)total_length / result.non_empty_buckets;
    }

    return result;
}

IndexEntry Index::read_entry(std::istream& file, std::uint64_t entry_offset) const {
    IndexEntry entry{};

    if (!entry_offset) {
        return entry;
    }

    file.seekg(entry_offset);
    if (!file) {
        throw std::runtime_error{"seek failed"};
    }
    if (utils::read_uint64(file, entry.db_offset) != utils::ReadStatus::Success) {
        throw std::runtime_error{"could not read db offset"};
    }
    if (utils::read_uint64(file, entry.next_entry_offset) != utils::ReadStatus::Success) {
        throw std::runtime_error{"could not read next entry offset"};
    }
    if (utils::read_string(file, entry.key, MAX_KEY_SIZE) != utils::ReadStatus::Success) {
        throw std::runtime_error{"could not read key"};
    }

    return entry;
}

bool Index::empty() const {
    // note: the index is considered empty only when all buckets point to a null entry
    try {
        std::ifstream file{path_, std::ios::binary};
        if (!file) {
            throw std::runtime_error{"Could not open index file: " + path_.string()};
        }

        std::uint64_t idx_chain_offset = 0;
        file.seekg(header_size());
        if (!file) {
            throw std::runtime_error{"seek failed"};
        }

        utils::ReadStatus read_status{};
        for (std::uint64_t i = 0; i < num_buckets_; i++) {
            read_status = utils::read_uint64(file, idx_chain_offset);
            // std::cout << "read status: " << static_cast<std::uint8_t>(read_status) << "\n";

            if (read_status != utils::ReadStatus::Success) {
                throw std::runtime_error{"could not read chain_offset from bucket"};
            }

            if (idx_chain_offset) {
                return false;
            }
        }

    } catch (const std::ios_base::failure& error) {
        throw std::runtime_error{"Could not read index file: " + path_.string()};
    }

    return true;
}

} // namespace zidanedb
