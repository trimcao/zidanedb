#include <catch2/catch_test_macros.hpp>

#include "constants.h"
#include "index.h"
#include "zidanedb/database.h"
#include "zidanedb/index_stats.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {

// Owns both temporary files, removing leftovers before use and cleaning up on destruction.
class TemporaryDatabaseFile {
  public:
    explicit TemporaryDatabaseFile(const std::string& filename)
        : path_{std::filesystem::temp_directory_path() / filename}, idx_path_{path_} {

        idx_path_ += ".idx";
        remove();
    }

    ~TemporaryDatabaseFile() { remove(); }

    const std::filesystem::path& path() const { return path_; }

    const std::filesystem::path& idx_path() const { return idx_path_; }

  private:
    std::filesystem::path path_;
    std::filesystem::path idx_path_;

    void remove() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
        std::filesystem::remove(idx_path_, ignored);
    }
};

// Restore permissions even when an assertion aborts or the test is skipped.
class ScopedReadOnlyFile {
  public:
    explicit ScopedReadOnlyFile(const std::filesystem::path& path)
        : path_{path}, original_permissions_{std::filesystem::status(path).permissions()} {
        const auto write_permissions = std::filesystem::perms::owner_write |
                                       std::filesystem::perms::group_write |
                                       std::filesystem::perms::others_write;
        std::filesystem::permissions(path_, write_permissions,
                                     std::filesystem::perm_options::remove);
    }

    ~ScopedReadOnlyFile() {
        std::error_code ignored;
        std::filesystem::permissions(path_, original_permissions_,
                                     std::filesystem::perm_options::replace, ignored);
    }

  private:
    std::filesystem::path path_;
    std::filesystem::perms original_permissions_;
};

} // namespace

TEST_CASE("get returns no value for a missing key") {
    TemporaryDatabaseFile file{
        "get-missing-key.zdb",
    };
    zidanedb::Database db{file.path()};
    const auto result = db.get("missing");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("put stores a value") {
    TemporaryDatabaseFile file{
        "put.zdb",
    };
    zidanedb::Database db{file.path()};
    db.put("player", "Bellingham");
    const auto result = db.get("player");
    REQUIRE(result.has_value());
    REQUIRE(*result == "Bellingham");
}

TEST_CASE("put replaces an existing value") {
    TemporaryDatabaseFile file{"put-existing.zdb"};
    zidanedb::Database db{file.path()};
    db.put("player", "Bellingham");
    db.put("player", "Ronaldo");
    const auto result = db.get("player");
    REQUIRE(result.has_value());
    REQUIRE(*result == "Ronaldo");
}

TEST_CASE("erase removes an existing key") {
    TemporaryDatabaseFile file{"erase.zdb"};
    zidanedb::Database db{file.path()};
    db.put("player", "Bellingham");
    const auto existed = db.erase("player");
    REQUIRE(existed);
    REQUIRE_FALSE(db.get("player").has_value());
}

TEST_CASE("erase returns false for a missing key") {
    TemporaryDatabaseFile file{"erase-missing-key.zdb"};
    zidanedb::Database db{file.path()};
    REQUIRE_FALSE(db.erase("missing"));
}

TEST_CASE("put persists values after reopening") {
    TemporaryDatabaseFile file{"zidanedb-put-persistence-test.zdb"};

    std::cout << "db file: " << file.path().string() << '\n';

    {
        zidanedb::Database database{file.path()};

        database.put("player", "Zidane");
        database.put("team", "Real Madrid");
    } // the first Database is destroyed here

    {
        const zidanedb::Database database{file.path()};

        const auto player = database.get("player");
        const auto team = database.get("team");

        REQUIRE(player.has_value());
        REQUIRE(*player == "Zidane");
        REQUIRE(team.has_value());
        REQUIRE(*team == "Real Madrid");
    }
}

TEST_CASE("replaced values remain replaced after reopening") {
    TemporaryDatabaseFile file{"zidanedb-replace-persistence-test.zdb"};

    {
        zidanedb::Database database{file.path()};
        database.put("player", "Zidane");
        database.put("player", "Ronaldo");
    }

    {
        zidanedb::Database database{file.path()};
        const auto player = database.get("player");
        REQUIRE(player.has_value());
        REQUIRE(*player == "Ronaldo");
    }
}

TEST_CASE("erased values remain erased after reopening") {
    TemporaryDatabaseFile file{"zidanedb-erase-persistence-test.zdb"};

    {
        zidanedb::Database database{file.path()};
        database.put("player", "Ronaldo");
        database.put("nationality", "Portugal");
        database.put("position", "Winger");
        REQUIRE(database.erase("position"));
    }

    {
        zidanedb::Database database{file.path()};
        const auto nation = database.get("nationality");
        const auto position = database.get("position");
        REQUIRE(nation.has_value());
        REQUIRE(*nation == "Portugal");
        REQUIRE_FALSE(position.has_value());
    }
}

TEST_CASE("multi-line value should work") {
    TemporaryDatabaseFile file{"zidanedb-multiline-persistence-test.zdb"};

    {
        zidanedb::Database database{file.path()};
        database.put("player", "Ronaldo");
        database.put("multi", "line1\nline2");
    }

    {
        zidanedb::Database database{file.path()};
        const auto multi = database.get("multi");
        REQUIRE(multi.has_value());
        REQUIRE(*multi == "line1\nline2");
    }
}

TEST_CASE("a new index entry persists after reopening", "[persistence][regression]") {
    TemporaryDatabaseFile file{"new-index-entry-persistence-test.zdb"};

    REQUIRE_FALSE(std::filesystem::exists(file.path()));
    REQUIRE_FALSE(std::filesystem::exists(file.idx_path()));

    std::uintmax_t initial_index_size = 0;
    {
        zidanedb::Database database{file.path()};
        // Construction already writes a header and bucket table, without any entries.
        initial_index_size = std::filesystem::file_size(file.idx_path());
        REQUIRE(initial_index_size > 0);
        database.put("new-key", "new-value");
    }

    REQUIRE(std::filesystem::exists(file.path()));
    REQUIRE(std::filesystem::exists(file.idx_path()));

    REQUIRE(std::filesystem::file_size(file.idx_path()) > initial_index_size);

    {
        const zidanedb::Database reopened{file.path()};

        const auto result = reopened.get("new-key");

        REQUIRE(result.has_value());
        REQUIRE(*result == "new-value");
    }
}

TEST_CASE("test delete head in an index collision chain") {
    // NOTE: we insert new index entry at head, that's why last in = head
    TemporaryDatabaseFile file{"index-collision-chain-del-head.zdb"};

    REQUIRE_FALSE(std::filesystem::exists(file.path()));
    REQUIRE_FALSE(std::filesystem::exists(file.idx_path()));

    {
        zidanedb::Database database{file.path(), 1};
        database.put("key1", "value1");
        database.put("key2", "value2");
        database.put("key3", "value3");
        database.put("key4", "value4");
    }

    REQUIRE(std::filesystem::exists(file.path()));
    REQUIRE(std::filesystem::exists(file.idx_path()));

    {
        zidanedb::Database reopened{file.path()};

        auto result = reopened.get("key1");
        REQUIRE(result.has_value());
        REQUIRE(*result == "value1");

        result = reopened.get("key2");
        REQUIRE(result.has_value());
        REQUIRE(*result == "value2");

        result = reopened.get("key3");
        REQUIRE(result.has_value());
        REQUIRE(*result == "value3");

        result = reopened.get("key4");
        REQUIRE(result.has_value());
        REQUIRE(*result == "value4");

        REQUIRE(reopened.erase("key4"));
    }

    {
        zidanedb::Database reopened{file.path()};

        auto result = reopened.get("key1");
        REQUIRE(result.has_value());
        REQUIRE(*result == "value1");

        result = reopened.get("key2");
        REQUIRE(result.has_value());
        REQUIRE(*result == "value2");

        result = reopened.get("key3");
        REQUIRE(result.has_value());
        REQUIRE(*result == "value3");

        result = reopened.get("key4");
        REQUIRE_FALSE(result.has_value());
    }
}

TEST_CASE("test delete middle in an index collision chain") {
    TemporaryDatabaseFile file{"index-collision-chain-del-middle.zdb"};

    REQUIRE_FALSE(std::filesystem::exists(file.path()));
    REQUIRE_FALSE(std::filesystem::exists(file.idx_path()));

    {
        zidanedb::Database database{file.path(), 1};
        database.put("key1", "value1");
        database.put("key2", "value2");
        database.put("key3", "value3");
    }

    REQUIRE(std::filesystem::exists(file.path()));
    REQUIRE(std::filesystem::exists(file.idx_path()));

    {
        zidanedb::Database reopened{file.path()};

        auto result = reopened.get("key1");
        REQUIRE(result.has_value());
        REQUIRE(*result == "value1");

        result = reopened.get("key2");
        REQUIRE(result.has_value());
        REQUIRE(*result == "value2");

        result = reopened.get("key3");
        REQUIRE(result.has_value());
        REQUIRE(*result == "value3");

        REQUIRE(reopened.erase("key2"));
    }

    {
        zidanedb::Database reopened{file.path()};

        auto result = reopened.get("key1");
        REQUIRE(result.has_value());
        REQUIRE(*result == "value1");

        result = reopened.get("key2");
        REQUIRE_FALSE(result.has_value());

        result = reopened.get("key3");
        REQUIRE(result.has_value());
        REQUIRE(*result == "value3");
    }
}

TEST_CASE("test delete tail in an index collision chain") {
    TemporaryDatabaseFile file{"index-collision-chain-del-tail.zdb"};

    REQUIRE_FALSE(std::filesystem::exists(file.path()));
    REQUIRE_FALSE(std::filesystem::exists(file.idx_path()));

    {
        zidanedb::Database database{file.path(), 1};
        database.put("key1", "value1");
        database.put("key2", "value2");
        database.put("key3", "value3");
    }

    REQUIRE(std::filesystem::exists(file.path()));
    REQUIRE(std::filesystem::exists(file.idx_path()));

    {
        zidanedb::Database reopened{file.path()};

        auto result = reopened.get("key1");
        REQUIRE(result.has_value());
        REQUIRE(*result == "value1");

        result = reopened.get("key2");
        REQUIRE(result.has_value());
        REQUIRE(*result == "value2");

        result = reopened.get("key3");
        REQUIRE(result.has_value());
        REQUIRE(*result == "value3");

        REQUIRE(reopened.erase("key1"));
    }

    {
        zidanedb::Database reopened{file.path()};

        auto result = reopened.get("key1");
        REQUIRE_FALSE(result.has_value());

        result = reopened.get("key2");
        REQUIRE(result.has_value());
        REQUIRE(*result == "value2");

        result = reopened.get("key3");
        REQUIRE(result.has_value());
        REQUIRE(*result == "value3");
    }
}

TEST_CASE("overwrite in an index collision chain") {
    TemporaryDatabaseFile file{"index-collision-chain-overwrite.zdb"};

    REQUIRE_FALSE(std::filesystem::exists(file.path()));
    REQUIRE_FALSE(std::filesystem::exists(file.idx_path()));

    {
        zidanedb::Database database{file.path(), 1};
        database.put("key1", "value1");
        database.put("key2", "value2");
        database.put("key3", "value3");
    }

    REQUIRE(std::filesystem::exists(file.path()));
    REQUIRE(std::filesystem::exists(file.idx_path()));

    {
        zidanedb::Database reopened{file.path()};

        auto result = reopened.get("key1");
        REQUIRE(result.has_value());
        REQUIRE(*result == "value1");

        result = reopened.get("key2");
        REQUIRE(result.has_value());
        REQUIRE(*result == "value2");

        result = reopened.get("key3");
        REQUIRE(result.has_value());
        REQUIRE(*result == "value3");

        reopened.put("key1", "value11");
        reopened.put("key2", "value22");
    }

    {
        zidanedb::Database reopened{file.path()};

        auto result = reopened.get("key1");
        REQUIRE(result.has_value());
        REQUIRE(*result == "value11");

        result = reopened.get("key2");
        REQUIRE(result.has_value());
        REQUIRE(*result == "value22");

        result = reopened.get("key3");
        REQUIRE(result.has_value());
        REQUIRE(*result == "value3");

        reopened.put("key1", "value111");
        reopened.put("key1", "value1111");
    }

    {
        zidanedb::Database reopened{file.path()};

        auto result = reopened.get("key1");
        REQUIRE(result.has_value());
        REQUIRE(*result == "value1111");

        result = reopened.get("key2");
        REQUIRE(result.has_value());
        REQUIRE(*result == "value22");

        result = reopened.get("key3");
        REQUIRE(result.has_value());
        REQUIRE(*result == "value3");
    }
}

TEST_CASE("keys with empty values") {
    TemporaryDatabaseFile file{"keys-with-empty-values.zdb"};

    REQUIRE_FALSE(std::filesystem::exists(file.path()));
    REQUIRE_FALSE(std::filesystem::exists(file.idx_path()));

    {
        zidanedb::Database database{file.path()};
        database.put("key1", "");
        database.put("key2", "");
        database.put("key3", "");
    }

    REQUIRE(std::filesystem::exists(file.path()));
    REQUIRE(std::filesystem::exists(file.idx_path()));

    {
        zidanedb::Database reopened{file.path()};

        auto result = reopened.get("key1");
        REQUIRE(result.has_value());
        REQUIRE(*result == "");

        result = reopened.get("key2");
        REQUIRE(result.has_value());
        REQUIRE(*result == "");

        result = reopened.get("key3");
        REQUIRE(result.has_value());
        REQUIRE(*result == "");

        reopened.put("key1", "");
        reopened.put("key2", "");
    }

    {
        zidanedb::Database reopened{file.path()};

        auto result = reopened.get("key1");
        REQUIRE(result.has_value());
        REQUIRE(*result == "");

        result = reopened.get("key2");
        REQUIRE(result.has_value());
        REQUIRE(*result == "");

        result = reopened.get("key3");
        REQUIRE(result.has_value());
        REQUIRE(*result == "");

        reopened.put("key1", "value111");
    }

    {
        zidanedb::Database reopened{file.path()};

        auto result = reopened.get("key1");
        REQUIRE(result.has_value());
        REQUIRE(*result == "value111");

        result = reopened.get("key2");
        REQUIRE(result.has_value());
        REQUIRE(*result == "");

        result = reopened.get("key3");
        REQUIRE(result.has_value());
        REQUIRE(*result == "");
    }
}

TEST_CASE("statistics for a new database are empty", "[stats][regression]") {
    TemporaryDatabaseFile file{"stats-empty.zdb"};
    const zidanedb::Database database{file.path(), 4};

    const auto stats = database.get_index_stats();
    CHECK(stats.num_buckets == 4);
    CHECK(stats.empty_buckets == 4);
    CHECK(stats.non_empty_buckets == 0);
    CHECK(stats.avg_chain_length == 0.0);
    CHECK(stats.max_chain_length == 0);
}

TEST_CASE("statistics count every entry in a collision chain", "[stats][regression]") {
    TemporaryDatabaseFile file{"stats-collision-chain.zdb"};

    {
        // One bucket forces all three keys into the same chain.
        zidanedb::Database database{file.path(), 1};
        database.put("a", "1");
        database.put("b", "2");
        database.put("c", "3");
    }

    const zidanedb::Database reopened{file.path(), 1};
    const auto stats = reopened.get_index_stats();
    CHECK(stats.num_buckets == 1);
    CHECK(stats.empty_buckets == 0);
    CHECK(stats.non_empty_buckets == 1);
    CHECK(stats.avg_chain_length == 3.0);
    CHECK(stats.max_chain_length == 3);
}

TEST_CASE("statistics average only occupied buckets", "[stats][regression]") {
    TemporaryDatabaseFile file{"stats-occupied-buckets.zdb"};
    zidanedb::Database database{file.path(), 4};

    // With the current FNV-1a hash, a and e share a bucket; b uses another.
    database.put("a", "1");
    database.put("e", "2");
    database.put("b", "3");

    const auto stats = database.get_index_stats();
    CHECK(stats.num_buckets == 4);
    CHECK(stats.empty_buckets == 2);
    CHECK(stats.non_empty_buckets == 2);
    CHECK(stats.avg_chain_length == 1.5);
    CHECK(stats.max_chain_length == 2);
}

TEST_CASE("statistics follow deletions until the index is empty", "[stats][regression]") {
    TemporaryDatabaseFile file{"stats-after-deletions.zdb"};

    {
        zidanedb::Database database{file.path(), 1};
        database.put("a", "1");
        database.put("b", "2");
        database.put("c", "3");

        REQUIRE(database.erase("b"));
        const auto stats = database.get_index_stats();
        CHECK(stats.empty_buckets == 0);
        CHECK(stats.non_empty_buckets == 1);
        CHECK(stats.avg_chain_length == 2.0);
        CHECK(stats.max_chain_length == 2);

        REQUIRE(database.erase("a"));
        REQUIRE(database.erase("c"));
        const auto empty_stats = database.get_index_stats();
        CHECK(empty_stats.empty_buckets == 1);
        CHECK(empty_stats.non_empty_buckets == 0);
        CHECK(empty_stats.avg_chain_length == 0.0);
        CHECK(empty_stats.max_chain_length == 0);
    }

    const zidanedb::Database reopened{file.path(), 1};
    const auto stats = reopened.get_index_stats();
    CHECK(stats.empty_buckets == 1);
    CHECK(stats.non_empty_buckets == 0);
    CHECK(stats.avg_chain_length == 0.0);
    CHECK(stats.max_chain_length == 0);
}

TEST_CASE("strings at the size limits persist after reopening", "[limits][regression]") {
    TemporaryDatabaseFile file{"string-size-boundaries.zdb"};
    std::string key = "key";
    std::string value = "value";

    SECTION("key at the maximum length") { key.assign(zidanedb::MAX_KEY_SIZE, 'k'); }
    SECTION("value at the maximum length") { value.assign(zidanedb::MAX_VALUE_SIZE, 'v'); }
    SECTION("empty key and value") {
        key.clear();
        value.clear();
    }

    {
        zidanedb::Database database{file.path(), 1};
        database.put(key, value);
        const auto result = database.get(key);
        REQUIRE(result.has_value());
        CHECK(result->size() == value.size());
        // Keep Catch2 from printing an entire 16-MiB value on failure.
        CHECK((*result == value));
    }

    const zidanedb::Database reopened{file.path(), 1};
    const auto result = reopened.get(key);
    REQUIRE(result.has_value());
    CHECK(result->size() == value.size());
    CHECK((*result == value));
}

TEST_CASE("oversized writes leave existing files and values unchanged", "[limits][regression]") {
    TemporaryDatabaseFile file{"oversized-write-existing.zdb"};
    std::string key = "existing";
    std::string value = "replacement";

    SECTION("key is one byte too long") { key.assign(zidanedb::MAX_KEY_SIZE + 1, 'k'); }
    SECTION("value is one byte too long") { value.assign(zidanedb::MAX_VALUE_SIZE + 1, 'v'); }

    {
        zidanedb::Database database{file.path(), 1};
        database.put("existing", "original");
        const auto database_size = std::filesystem::file_size(file.path());
        const auto index_size = std::filesystem::file_size(file.idx_path());

        CHECK_THROWS_AS(database.put(key, value), std::runtime_error);
        CHECK(std::filesystem::file_size(file.path()) == database_size);
        CHECK(std::filesystem::file_size(file.idx_path()) == index_size);
        CHECK(database.get("existing") == "original");

        // A rejected write must not prevent the next valid write from succeeding.
        database.put("after", "still works");
    }

    const zidanedb::Database reopened{file.path(), 1};
    CHECK(reopened.get("existing") == "original");
    CHECK(reopened.get("after") == "still works");
    if (key != "existing") {
        CHECK_FALSE(reopened.get(key).has_value());
    }
}

TEST_CASE("an oversized first write does not create a database file", "[limits][regression]") {
    TemporaryDatabaseFile file{"oversized-write-new.zdb"};
    std::string key = "key";
    std::string value = "value";

    SECTION("key is one byte too long") { key.assign(zidanedb::MAX_KEY_SIZE + 1, 'k'); }
    SECTION("value is one byte too long") { value.assign(zidanedb::MAX_VALUE_SIZE + 1, 'v'); }

    zidanedb::Database database{file.path(), 1};
    REQUIRE_FALSE(std::filesystem::exists(file.path()));
    const auto index_size = std::filesystem::file_size(file.idx_path());

    CHECK_THROWS_AS(database.put(key, value), std::runtime_error);
    CHECK_FALSE(std::filesystem::exists(file.path()));
    CHECK(std::filesystem::file_size(file.idx_path()) == index_size);
}

TEST_CASE("Index set rejects oversized keys before writing", "[index][limits][regression]") {
    TemporaryDatabaseFile file{"oversized-index-key.zdb"};

    {
        zidanedb::Index index{file.idx_path(), 1};
        index.set("existing", 42);
        const auto index_size = std::filesystem::file_size(file.idx_path());

        CHECK_THROWS_AS(index.set(std::string(zidanedb::MAX_KEY_SIZE + 1, 'k'), 99),
                        std::runtime_error);
        CHECK(std::filesystem::file_size(file.idx_path()) == index_size);
        CHECK(index.find("existing") == 42);
    }

    const zidanedb::Index reopened{file.idx_path(), 1};
    CHECK(reopened.find("existing") == 42);
}

TEST_CASE("index filenames preserve the complete database filename", "[filenames][regression]") {
    std::string filename;
    SECTION("simple filename") { filename = "filename-convention.zdb"; }
    SECTION("filename with additional dots") { filename = "filename-convention.backup.zdb"; }

    TemporaryDatabaseFile file{filename};
    {
        zidanedb::Database database{file.path(), 1};
        database.put("player", "Zidane");

        CHECK(std::filesystem::exists(file.path()));
        CHECK(std::filesystem::exists(file.idx_path()));
    }

    const zidanedb::Database reopened{file.path(), 1};
    CHECK(reopened.get("player") == "Zidane");
}

TEST_CASE("unsupported database filenames are rejected before creating files",
          "[filenames][regression]") {
    std::string filename;
    SECTION("no extension") { filename = "filename-invalid"; }
    SECTION("another extension") { filename = "filename-invalid.backup"; }
    SECTION("old index extension") { filename = "filename-invalid.zidx"; }
    SECTION("current index extension") { filename = "filename-invalid.zdb.idx"; }
    SECTION("uppercase extension") { filename = "filename-invalid.ZDB"; }

    TemporaryDatabaseFile file{filename};
    CHECK_THROWS_AS((zidanedb::Database{file.path(), 1}), std::runtime_error);
    CHECK_FALSE(std::filesystem::exists(file.path()));
    CHECK_FALSE(std::filesystem::exists(file.idx_path()));
}

TEST_CASE("different supported database filenames keep independent indexes",
          "[filenames][regression]") {
    TemporaryDatabaseFile first{"filename-independent.zdb"};
    TemporaryDatabaseFile second{"filename-independent.backup.zdb"};

    {
        zidanedb::Database first_database{first.path(), 1};
        first_database.put("shared", "first");
        first_database.put("first-only", "first value");

        zidanedb::Database second_database{second.path(), 1};
        second_database.put("shared", "second");
        second_database.put("second-only", "second value");
    }

    CHECK(std::filesystem::exists(first.idx_path()));
    CHECK(std::filesystem::exists(second.idx_path()));
    const zidanedb::Database first_reopened{first.path(), 1};
    const zidanedb::Database second_reopened{second.path(), 1};

    CHECK(first_reopened.get("shared") == "first");
    CHECK(first_reopened.get("first-only") == "first value");
    CHECK_FALSE(first_reopened.get("second-only").has_value());
    CHECK(second_reopened.get("shared") == "second");
    CHECK(second_reopened.get("second-only") == "second value");
    CHECK_FALSE(second_reopened.get("first-only").has_value());
}

TEST_CASE("an existing database with a missing index is rejected without replacing the index",
          "[filenames][regression]") {
    TemporaryDatabaseFile file{"filename-missing-index.zdb"};
    {
        zidanedb::Database database{file.path(), 1};
        database.put("player", "Zidane");
    }

    const auto database_size = std::filesystem::file_size(file.path());
    REQUIRE(std::filesystem::remove(file.idx_path()));

    CHECK_THROWS_AS((zidanedb::Database{file.path(), 1}), std::runtime_error);
    CHECK_FALSE(std::filesystem::exists(file.idx_path()));
    REQUIRE(std::filesystem::exists(file.path()));
    CHECK(std::filesystem::file_size(file.path()) == database_size);
}

TEST_CASE("an empty index without a database file can be reopened before the first write",
          "[filenames][regression]") {
    TemporaryDatabaseFile file{"filename-empty-index.zdb"};
    {
        const zidanedb::Database database{file.path(), 1};
        CHECK_FALSE(database.get("missing").has_value());
    }

    REQUIRE(std::filesystem::exists(file.idx_path()));
    REQUIRE_FALSE(std::filesystem::exists(file.path()));
    {
        zidanedb::Database reopened{file.path(), 1};
        CHECK_FALSE(reopened.get("missing").has_value());
        reopened.put("player", "Zidane");
    }

    const zidanedb::Database persisted{file.path(), 1};
    CHECK(persisted.get("player") == "Zidane");
}

TEST_CASE("a populated index with a missing database file is rejected", "[filenames][regression]") {
    TemporaryDatabaseFile file{"filename-missing-database.zdb"};
    {
        zidanedb::Database database{file.path(), 1};
        database.put("old", "original");
    }

    const auto index_size = std::filesystem::file_size(file.idx_path());
    REQUIRE(std::filesystem::remove(file.path()));

    // Stale offsets must not be reused in a newly created database file.
    CHECK_THROWS_AS((zidanedb::Database{file.path(), 1}), std::runtime_error);
    CHECK_FALSE(std::filesystem::exists(file.path()));
    REQUIRE(std::filesystem::exists(file.idx_path()));
    CHECK(std::filesystem::file_size(file.idx_path()) == index_size);
}

TEST_CASE("an index with only erased entries can reopen without its database file",
          "[filenames][empty][regression]") {
    TemporaryDatabaseFile file{"filename-erased-index.zdb"};
    std::uintmax_t empty_index_size = 0;
    {
        zidanedb::Database database{file.path(), 1};
        empty_index_size = std::filesystem::file_size(file.idx_path());
        database.put("old", "original");
        REQUIRE(database.erase("old"));
    }

    REQUIRE(std::filesystem::file_size(file.idx_path()) > empty_index_size);
    REQUIRE(std::filesystem::remove(file.path()));
    {
        zidanedb::Database reopened{file.path(), 1};
        CHECK_FALSE(reopened.get("old").has_value());
        reopened.put("new", "replacement");
        CHECK_FALSE(reopened.get("old").has_value());
    }

    const zidanedb::Database persisted{file.path(), 1};
    CHECK(persisted.get("new") == "replacement");
    CHECK_FALSE(persisted.get("old").has_value());
}

TEST_CASE("Database rejects zero buckets before creating either file", "[validation][regression]") {
    TemporaryDatabaseFile file{"database-zero-buckets.zdb"};

    CHECK_THROWS_AS((zidanedb::Database{file.path(), 0}), std::runtime_error);
    CHECK_FALSE(std::filesystem::exists(file.path()));
    CHECK_FALSE(std::filesystem::exists(file.idx_path()));
}

TEST_CASE("database reads work with a read-only index", "[permissions][regression]") {
    TemporaryDatabaseFile file{"database-readonly-index.zdb"};
    bool populated = false;
    SECTION("empty index before the first write") { populated = false; }
    SECTION("populated index") { populated = true; }

    {
        zidanedb::Database database{file.path(), 4};
        if (populated) {
            database.put("player", "Zidane");
        }
    }

    const auto original_permissions = std::filesystem::status(file.idx_path()).permissions();
    {
        ScopedReadOnlyFile read_only{file.idx_path()};
        {
            // Opening with in|out does not truncate or write anything.
            std::fstream writer{file.idx_path(), std::ios::in | std::ios::out | std::ios::binary};
            if (writer.is_open()) {
                // Root privileges or some filesystems can bypass the permission bits.
                SKIP("Cannot enforce a read-only index for this user/filesystem");
            }
        }
        // Confirm it is readable, so the failed write-open was not a missing-file error.
        std::ifstream reader{file.idx_path(), std::ios::binary};
        REQUIRE(reader.is_open());

        const zidanedb::Database reopened{file.path(), 4};
        CHECK_FALSE(reopened.get("missing").has_value());
        const auto stats = reopened.get_index_stats();
        CHECK(stats.num_buckets == 4);
        if (populated) {
            CHECK(reopened.get("player") == "Zidane");
            CHECK(stats.non_empty_buckets == 1);
            CHECK(stats.max_chain_length == 1);
        } else {
            CHECK_FALSE(reopened.get("player").has_value());
            CHECK(stats.non_empty_buckets == 0);
            CHECK(stats.max_chain_length == 0);
        }

        const zidanedb::Index index{file.idx_path(), 4};
        CHECK(index.empty() == !populated);
    }
    CHECK(std::filesystem::status(file.idx_path()).permissions() == original_permissions);
}
