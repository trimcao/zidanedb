#include <catch2/catch_test_macros.hpp>

#include "constants.h"
#include "index.h"
#include "utils.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {

// Only owns a dedicated temporary index, never a real database file.
class TemporaryIndexFile {
  public:
    explicit TemporaryIndexFile(const std::string& filename)
        : path_{std::filesystem::temp_directory_path() / filename} {
        std::filesystem::remove(path_);
    }

    ~TemporaryIndexFile() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    const std::filesystem::path& path() const { return path_; }

  private:
    std::filesystem::path path_;
};

} // namespace

TEST_CASE("Index rejects zero requested buckets before creating a file",
          "[index][validation][regression]") {
    TemporaryIndexFile file{"index-zero-requested-buckets.zdb.idx"};

    CHECK_THROWS_AS((zidanedb::Index{file.path(), 0}), std::runtime_error);
    CHECK_FALSE(std::filesystem::exists(file.path()));
}

TEST_CASE("Index rejects a stored zero bucket count", "[index][validation][regression]") {
    TemporaryIndexFile file{"index-zero-stored-buckets.zdb.idx"};
    {
        const zidanedb::Index index{file.path(), 1};
    }
    const auto original_size = std::filesystem::file_size(file.path());

    {
        // Change only the count in a complete header; keep the bucket bytes intact.
        const auto bucket_count_offset =
            sizeof(std::uint32_t) + zidanedb::INDEX_MAGIC.size() + sizeof(std::uint32_t);
        std::fstream stream{file.path(), std::ios::in | std::ios::out | std::ios::binary};
        stream.exceptions(std::ios::failbit | std::ios::badbit);
        stream.seekp(static_cast<std::streamoff>(bucket_count_offset));
        zidanedb::utils::write_uint64(stream, 0);
        stream.flush();
    }

    REQUIRE(std::filesystem::file_size(file.path()) == original_size);
    // The requested count is valid, so rejection must come from the stored header.
    CHECK_THROWS_AS((zidanedb::Index{file.path(), 1}), std::runtime_error);
    CHECK(std::filesystem::file_size(file.path()) == original_size);
}

TEST_CASE("Index rejects invalid magic and unsupported versions",
          "[index][validation][regression]") {
    TemporaryIndexFile file{"index-invalid-header-fields.zdb.idx"};
    {
        const zidanedb::Index index{file.path(), 1};
    }
    const auto original_size = std::filesystem::file_size(file.path());

    {
        std::fstream stream{file.path(), std::ios::in | std::ios::out | std::ios::binary};
        stream.exceptions(std::ios::failbit | std::ios::badbit);
        const auto version_offset = sizeof(std::uint32_t) + zidanedb::INDEX_MAGIC.size();

        SECTION("magic has the correct length but different contents") {
            // Preserve the length prefix and all other fields: corrupt one magic byte.
            REQUIRE_FALSE(zidanedb::INDEX_MAGIC.empty());
            REQUIRE(zidanedb::INDEX_MAGIC.front() != '?');
            stream.seekp(static_cast<std::streamoff>(sizeof(std::uint32_t)));
            stream.put('?');
        }
        SECTION("version zero is unsupported") {
            REQUIRE(zidanedb::INDEX_VERSION != 0);
            stream.seekp(static_cast<std::streamoff>(version_offset));
            zidanedb::utils::write_uint32(stream, 0);
        }
        SECTION("a newer version is unsupported") {
            stream.seekp(static_cast<std::streamoff>(version_offset));
            zidanedb::utils::write_uint32(stream, zidanedb::INDEX_VERSION + 1);
        }
        stream.flush();
    }

    // This is invalid metadata, not a truncated-file test.
    REQUIRE(std::filesystem::file_size(file.path()) == original_size);
    CHECK_THROWS_AS((zidanedb::Index{file.path(), 1}), std::runtime_error);
    CHECK(std::filesystem::file_size(file.path()) == original_size);
}

TEST_CASE("Index empty checks both early and late buckets", "[index][empty][regression]") {
    TemporaryIndexFile file{"index-empty-bucket-positions.zdb.idx"};
    std::string key;
    std::uint64_t expected_bucket = 0;
    SECTION("first bucket contains an entry") { key = "a"; }
    SECTION("last bucket contains an entry") {
        key = "d";
        expected_bucket = 3;
    }
    REQUIRE(zidanedb::utils::fnv1a(key) % 4 == expected_bucket);

    {
        zidanedb::Index index{file.path(), 4};
        REQUIRE(index.empty());
        index.set(key, 42);
        CHECK_FALSE(index.empty());
    }

    const zidanedb::Index reopened{file.path(), 4};
    CHECK_FALSE(reopened.empty());
}

TEST_CASE("Index empty becomes true after erasing all entries", "[index][empty][regression]") {
    TemporaryIndexFile file{"index-empty-after-erase.zdb.idx"};
    {
        zidanedb::Index index{file.path(), 1};
        REQUIRE(index.empty());
        const auto empty_file_size = std::filesystem::file_size(file.path());
        index.set("first", 42);
        index.set("second", 99);
        const auto populated_file_size = std::filesystem::file_size(file.path());
        REQUIRE(populated_file_size > empty_file_size);
        CHECK_FALSE(index.empty());

        REQUIRE(index.erase("first"));
        CHECK_FALSE(index.empty());
        REQUIRE(index.erase("second"));
        CHECK(index.empty());

        // Deleted entries remain on disk: logical emptiness must not depend on file size.
        CHECK(std::filesystem::file_size(file.path()) == populated_file_size);
    }

    const zidanedb::Index reopened{file.path(), 1};
    CHECK(reopened.empty());
    CHECK(reopened.stats().non_empty_buckets == 0);
}

TEST_CASE("Index empty reports a missing index file as an error", "[index][empty][regression]") {
    TemporaryIndexFile file{"index-empty-missing-file.zdb.idx"};
    const zidanedb::Index index{file.path(), 1};
    REQUIRE(std::filesystem::remove(file.path()));

    // An unreadable index is not evidence that it contains no live keys.
    CHECK_THROWS_AS(index.empty(), std::runtime_error);
    CHECK_FALSE(std::filesystem::exists(file.path()));
}

TEST_CASE("Index rejects truncated headers when reopening", "[index][truncation][regression]") {
    TemporaryIndexFile file{"index-truncated-header.zdb.idx"};
    {
        const zidanedb::Index index{file.path(), 2};
    }

    // Header: [magic_length:u32][magic_bytes][version:u32][bucket_count:u64].
    const std::uintmax_t magic_end = sizeof(std::uint32_t) + zidanedb::INDEX_MAGIC.size();
    const std::uintmax_t version_end = magic_end + sizeof(std::uint32_t);
    const std::uintmax_t header_end = version_end + sizeof(std::uint64_t);
    std::uintmax_t bytes_to_keep = 0;

    SECTION("entire header is missing") { bytes_to_keep = 0; }
    SECTION("magic length prefix is incomplete") { bytes_to_keep = sizeof(std::uint32_t) - 1; }
    SECTION("magic bytes are incomplete") { bytes_to_keep = magic_end - 1; }
    SECTION("version is incomplete") { bytes_to_keep = version_end - 1; }
    SECTION("bucket count is incomplete") { bytes_to_keep = header_end - 1; }

    CAPTURE(bytes_to_keep);
    REQUIRE(bytes_to_keep < std::filesystem::file_size(file.path()));
    // Keep this many bytes from the beginning and discard everything after them.
    std::filesystem::resize_file(file.path(), bytes_to_keep);
    REQUIRE(std::filesystem::file_size(file.path()) == bytes_to_keep);

    CHECK_THROWS_AS((zidanedb::Index{file.path(), 2}), std::runtime_error);
    CHECK(std::filesystem::file_size(file.path()) == bytes_to_keep);
}

TEST_CASE("Index rejects truncated bucket tables when reopening",
          "[index][truncation][regression]") {
    TemporaryIndexFile file{"index-truncated-buckets.zdb.idx"};
    constexpr std::uint64_t bucket_count = 3;
    {
        const zidanedb::Index index{file.path(), bucket_count};
    }

    const auto complete_size = std::filesystem::file_size(file.path());
    const auto header_size = complete_size - bucket_count * sizeof(std::uint64_t);
    std::uintmax_t bytes_to_keep = header_size;

    SECTION("all buckets are missing") { bytes_to_keep = header_size; }
    SECTION("first bucket is incomplete") {
        bytes_to_keep = header_size + sizeof(std::uint64_t) - 1;
    }
    SECTION("last bucket is incomplete") { bytes_to_keep = complete_size - 1; }
    SECTION("last bucket is entirely missing") {
        bytes_to_keep = complete_size - sizeof(std::uint64_t);
    }

    CAPTURE(header_size, bytes_to_keep);
    REQUIRE(bytes_to_keep >= header_size);
    REQUIRE(bytes_to_keep < complete_size);
    // Leave the header's declared count unchanged: it still promises three buckets.
    std::filesystem::resize_file(file.path(), bytes_to_keep);
    CHECK_THROWS_AS((zidanedb::Index{file.path(), bucket_count}), std::runtime_error);
    CHECK(std::filesystem::file_size(file.path()) == bytes_to_keep);
}

TEST_CASE("Index reads reject a bucket table truncated after construction",
          "[index][truncation][regression]") {
    TemporaryIndexFile file{"index-truncated-after-open.zdb.idx"};
    const zidanedb::Index index{file.path(), 4};
    REQUIRE(index.empty());
    REQUIRE(zidanedb::utils::fnv1a("d") % 4 == 3);

    // Construction already succeeded; exercise the individual read paths too.
    const auto complete_size = std::filesystem::file_size(file.path());
    std::filesystem::resize_file(file.path(), complete_size - 1);

    CHECK_THROWS_AS(index.empty(), std::runtime_error);
    CHECK_THROWS_AS(index.find("d"), std::runtime_error);
    CHECK_THROWS_AS(index.stats(), std::runtime_error);
}

TEST_CASE("Index operations reject truncated entries", "[index][truncation][regression]") {
    TemporaryIndexFile file{"index-truncated-entry.zdb.idx"};
    const std::string key = "key";
    std::uintmax_t entry_start = 0;
    {
        zidanedb::Index index{file.path(), 1};
        entry_start = std::filesystem::file_size(file.path());
        index.set(key, 42);
        REQUIRE(index.find(key) == 42);
    }

    // Entry: [db_offset:u64][next_entry:u64][key_length:u32][key_bytes].
    const auto db_offset_end = entry_start + sizeof(std::uint64_t);
    const auto next_entry_end = db_offset_end + sizeof(std::uint64_t);
    const auto key_length_end = next_entry_end + sizeof(std::uint32_t);
    const auto complete_size = std::filesystem::file_size(file.path());
    REQUIRE(complete_size == key_length_end + key.size());
    std::uintmax_t bytes_to_keep = entry_start;

    SECTION("bucket points at an entirely missing entry") { bytes_to_keep = entry_start; }
    SECTION("database offset is incomplete") { bytes_to_keep = db_offset_end - 1; }
    SECTION("next-entry offset is incomplete") { bytes_to_keep = next_entry_end - 1; }
    SECTION("key length prefix is incomplete") { bytes_to_keep = key_length_end - 1; }
    SECTION("key bytes are incomplete") { bytes_to_keep = complete_size - 1; }

    CAPTURE(entry_start, bytes_to_keep);
    REQUIRE(bytes_to_keep >= entry_start);
    REQUIRE(bytes_to_keep < complete_size);
    std::filesystem::resize_file(file.path(), bytes_to_keep);

    // The header and bucket table are intact. Entries are validated on access,
    // not by the constructor, which does not traverse the collision chains.
    zidanedb::Index reopened{file.path(), 1};
    CHECK_THROWS_AS(reopened.find(key), std::runtime_error);
    CHECK_THROWS_AS(reopened.stats(), std::runtime_error);
    CHECK_THROWS_AS(reopened.set(key, 99), std::runtime_error);
    CHECK_THROWS_AS(reopened.erase(key), std::runtime_error);
    CHECK(std::filesystem::file_size(file.path()) == bytes_to_keep);
}
