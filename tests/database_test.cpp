#include <catch2/catch_test_macros.hpp>

#include "constants.h"
#include "index.h"
#include "zidanedb/database.h"
#include "zidanedb/index_stats.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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

class TemporaryDirectory {
  public:
    explicit TemporaryDirectory(const std::string& name)
        : path_{std::filesystem::temp_directory_path() / name} {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const { return path_; }

  private:
    std::filesystem::path path_;
};

std::string read_file_bytes(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        throw std::runtime_error{"Could not read test file: " + path.string()};
    }

    return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void corrupt_last_byte(const std::filesystem::path& path) {
    std::fstream file{path, std::ios::in | std::ios::out | std::ios::binary};
    if (!file) {
        throw std::runtime_error{"Could not open test file: " + path.string()};
    }

    file.seekg(-1, std::ios::end);
    char byte{};
    file.read(&byte, 1);
    byte ^= 0x01;
    file.seekp(-1, std::ios::end);
    file.write(&byte, 1);
    file.flush();

    if (!file) {
        throw std::runtime_error{"Could not corrupt test file: " + path.string()};
    }
}

bool has_index_staging_directory(const std::filesystem::path& index_path) {
    auto parent = index_path.parent_path();
    if (parent.empty()) {
        parent = ".";
    }

    const auto prefix = "." + index_path.filename().string() + ".rebuild-";
    for (const auto& entry : std::filesystem::directory_iterator{parent}) {
        if (entry.is_directory() && entry.path().filename().string().starts_with(prefix)) {
            return true;
        }
    }

    return false;
}

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

TEST_CASE("an oversized first write does not modify an empty database file",
          "[limits][regression]") {
    TemporaryDatabaseFile file{"oversized-write-new.zdb"};
    std::string key = "key";
    std::string value = "value";

    SECTION("key is one byte too long") { key.assign(zidanedb::MAX_KEY_SIZE + 1, 'k'); }
    SECTION("value is one byte too long") { value.assign(zidanedb::MAX_VALUE_SIZE + 1, 'v'); }

    zidanedb::Database database{file.path(), 1};
    const auto index_size = std::filesystem::file_size(file.idx_path());
    const auto db_size = std::filesystem::file_size(file.path());

    CHECK_THROWS_AS(database.put(key, value), std::runtime_error);
    CHECK(std::filesystem::file_size(file.path()) == db_size);
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

TEST_CASE("an existing database with a missing index is rebuilt from the data log",
          "[database][recovery][filenames][regression]") {
    TemporaryDatabaseFile file{"filename-missing-index.zdb"};
    {
        zidanedb::Database database{file.path(), 1};
        database.put("player", "Zidane");
    }

    const auto database_size = std::filesystem::file_size(file.path());
    REQUIRE(std::filesystem::remove(file.idx_path()));

    {
        zidanedb::Database recovered{file.path(), 1};
        CHECK(recovered.get("player") == "Zidane");
        CHECK(std::filesystem::exists(file.idx_path()));
        CHECK_FALSE(has_index_staging_directory(file.idx_path()));
    }

    CHECK(std::filesystem::file_size(file.path()) == database_size);

    const zidanedb::Database reopened{file.path(), 1};
    CHECK(reopened.get("player") == "Zidane");
}

TEST_CASE("an invalid index is rebuilt from a valid data log",
          "[database][recovery][invalid-index][regression]") {
    TemporaryDatabaseFile file{"invalid-index-recovery.zdb"};
    {
        zidanedb::Database database{file.path(), 4};
        database.put("player", "Zidane");
        database.put("club", "Real Madrid");
    }
    const auto original_database = read_file_bytes(file.path());

    {
        // Keep the length prefix intact and corrupt the first magic byte. This
        // makes the existing file structurally invalid without truncating it.
        std::fstream stream{file.idx_path(), std::ios::in | std::ios::out | std::ios::binary};
        REQUIRE(stream.is_open());
        stream.seekp(static_cast<std::streamoff>(sizeof(std::uint32_t)));
        stream.put('?');
        stream.flush();
        REQUIRE(stream.good());
    }

    CHECK_THROWS_AS((zidanedb::Index{file.idx_path(), 4}), zidanedb::InvalidIndexError);

    {
        zidanedb::Database recovered{file.path(), 4};
        CHECK(recovered.get("player") == "Zidane");
        CHECK(recovered.get("club") == "Real Madrid");
        CHECK_FALSE(has_index_staging_directory(file.idx_path()));
    }

    CHECK(read_file_bytes(file.path()) == original_database);
    const zidanedb::Database reopened{file.path(), 4};
    CHECK(reopened.get("player") == "Zidane");
    CHECK(reopened.get("club") == "Real Madrid");
}

TEST_CASE("an empty index without a database file can be reopened before the first write",
          "[filenames][regression]") {
    TemporaryDatabaseFile file{"filename-empty-index.zdb"};
    {
        const zidanedb::Database database{file.path(), 1};
        CHECK_FALSE(database.get("missing").has_value());
    }

    REQUIRE(std::filesystem::exists(file.idx_path()));
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

TEST_CASE("rebuilding an index replays puts, replacements, and deletes",
          "[database][recovery][index-rebuild][regression]") {
    TemporaryDatabaseFile file{"rebuild-index-replays-log.zdb"};

    {
        zidanedb::Database database{file.path(), 4};
        database.put("a", "old");
        database.put("b", "value");
        database.put("a", "new");
        REQUIRE(database.erase("b"));

        // Removing the current index proves that rebuild_index() recreates it
        // from the data log instead of relying on the already-correct index.
        REQUIRE(std::filesystem::remove(file.idx_path()));
        database.rebuild_index();

        CHECK(database.get("a") == "new");
        CHECK_FALSE(database.get("b").has_value());
        CHECK_FALSE(has_index_staging_directory(file.idx_path()));
    }

    const zidanedb::Database reopened{file.path(), 4};
    CHECK(reopened.get("a") == "new");
    CHECK_FALSE(reopened.get("b").has_value());
}

TEST_CASE("a rebuilt index remains usable for reads and writes",
          "[database][recovery][index-rebuild][regression]") {
    TemporaryDatabaseFile file{"rebuilt-index-remains-usable.zdb"};

    {
        zidanedb::Database database{file.path(), 4};
        database.put("before", "rebuild");

        // This also exercises replacing an existing index file.
        database.rebuild_index();

        CHECK(database.get("before") == "rebuild");
        database.put("after", "replacement");
        CHECK(database.get("after") == "replacement");
        CHECK_FALSE(has_index_staging_directory(file.idx_path()));
    }

    const zidanedb::Database reopened{file.path(), 4};
    CHECK(reopened.get("before") == "rebuild");
    CHECK(reopened.get("after") == "replacement");
}

TEST_CASE("a failed index rebuild preserves the existing index and removes staging files",
          "[database][recovery][index-rebuild][checksum][regression]") {
    TemporaryDatabaseFile file{"failed-index-rebuild-preserves-index.zdb"};
    zidanedb::Database database{file.path(), 4};
    database.put("first", "valid");
    database.put("second", "will be corrupted");

    const auto original_index = read_file_bytes(file.idx_path());
    REQUIRE_FALSE(has_index_staging_directory(file.idx_path()));

    corrupt_last_byte(file.path());
    REQUIRE(database.scan_records().status == zidanedb::ScanStatus::ChecksumMismatch);
    REQUIRE_THROWS_AS(database.rebuild_index(), std::runtime_error);

    CHECK(read_file_bytes(file.idx_path()) == original_index);
    CHECK_FALSE(has_index_staging_directory(file.idx_path()));
}

TEST_CASE("rebuilding databases with identical filenames does not share temporary indexes",
          "[database][recovery][index-rebuild][filenames][regression]") {
    constexpr auto common_filename = "rebuild-identical-filename.zdb";
    TemporaryDirectory first_directory{"zidanedb-rebuild-first-directory"};
    TemporaryDirectory second_directory{"zidanedb-rebuild-second-directory"};

    const auto first_path = first_directory.path() / common_filename;
    const auto second_path = second_directory.path() / common_filename;

    // This is where the previous temp_directory_path()/filename approach
    // looked for its temporary index. A proper adjacent staging path ignores it.
    TemporaryDatabaseFile legacy_temporary_file{common_filename};
    {
        std::ofstream stale{legacy_temporary_file.idx_path(), std::ios::binary};
        REQUIRE(stale.is_open());
        stale << "stale temporary index";
    }
    const auto stale_contents = read_file_bytes(legacy_temporary_file.idx_path());

    zidanedb::Database first{first_path, 4};
    zidanedb::Database second{second_path, 4};
    first.put("shared", "first database");
    second.put("shared", "second database");

    first.rebuild_index();
    second.rebuild_index();

    CHECK(first.get("shared") == "first database");
    CHECK(second.get("shared") == "second database");
    CHECK(read_file_bytes(legacy_temporary_file.idx_path()) == stale_contents);
    CHECK_FALSE(has_index_staging_directory(first_path.string() + ".idx"));
    CHECK_FALSE(has_index_staging_directory(second_path.string() + ".idx"));
}

TEST_CASE("a header-only database rebuilds to an empty index",
          "[database][recovery][index-rebuild][empty][regression]") {
    TemporaryDatabaseFile file{"rebuild-empty-index.zdb"};

    {
        zidanedb::Database database{file.path(), 4};
        database.rebuild_index();

        CHECK_FALSE(database.get("missing").has_value());
        const auto stats = database.get_index_stats();
        CHECK(stats.num_buckets == 4);
        CHECK(stats.non_empty_buckets == 0);
        CHECK_FALSE(has_index_staging_directory(file.idx_path()));
    }

    const zidanedb::Database reopened{file.path(), 4};
    CHECK_FALSE(reopened.get("missing").has_value());
    CHECK(reopened.get_index_stats().non_empty_buckets == 0);
}

TEST_CASE("a stale dirty index is rebuilt when the database is reopened",
          "[database][recovery][metadata][regression]") {
    TemporaryDirectory directory{"zidanedb-stale-dirty-index"};
    const auto database_path = directory.path() / "stale-index.zdb";
    auto index_path = database_path;
    index_path += ".idx";
    const auto stale_index_path = directory.path() / "stale-index.snapshot";

    {
        zidanedb::Database database{database_path, 4};
        database.put("first", "already indexed");

        // Capture a valid but dirty index before the second log record exists.
        std::filesystem::copy_file(index_path, stale_index_path);
        database.put("second", "missing from snapshot");
    }
    const auto complete_database = read_file_bytes(database_path);

    std::filesystem::copy_file(stale_index_path, index_path,
                               std::filesystem::copy_options::overwrite_existing);
    {
        const zidanedb::Index stale_index{index_path, 4};
        REQUIRE_FALSE(stale_index.index_clean());
        REQUIRE(stale_index.find("first").has_value());
        REQUIRE_FALSE(stale_index.find("second").has_value());
    }

    {
        zidanedb::Database recovered{database_path, 4};
        CHECK(recovered.get("first") == "already indexed");
        CHECK(recovered.get("second") == "missing from snapshot");
        CHECK_FALSE(has_index_staging_directory(index_path));
    }

    CHECK(read_file_bytes(database_path) == complete_database);
    const zidanedb::Index recovered_index{index_path, 4};
    CHECK(recovered_index.index_clean());
    CHECK(recovered_index.indexed_up_to_offset() == std::filesystem::file_size(database_path));
}

TEST_CASE("close is idempotent and public operations reject a closed database",
          "[database][lifecycle][regression]") {
    TemporaryDatabaseFile file{"explicit-close.zdb"};
    zidanedb::Database database{file.path(), 4};
    database.put("player", "Zidane");

    REQUIRE_NOTHROW(database.close());
    CHECK_NOTHROW(database.close());

    CHECK_THROWS_AS(database.get("player"), std::logic_error);
    CHECK_THROWS_AS(database.put("club", "Real Madrid"), std::logic_error);
    CHECK_THROWS_AS(database.erase("player"), std::logic_error);
    CHECK_THROWS_AS(database.get_index_stats(), std::logic_error);
    CHECK_THROWS_AS(database.index_metadata_clean(), std::logic_error);
    CHECK_THROWS_AS(database.scan_records(), std::logic_error);
    CHECK_THROWS_AS(database.recover_records(), std::logic_error);
    CHECK_THROWS_AS(database.rebuild_index(), std::logic_error);
}

TEST_CASE("the destructor suppresses a failure while marking the index clean",
          "[database][lifecycle][regression]") {
    TemporaryDatabaseFile file{"destructor-close-failure.zdb"};
    {
        zidanedb::Database database{file.path(), 4};
        database.put("player", "Zidane");

        // close() will be unable to reopen this path to update the clean flag.
        REQUIRE(std::filesystem::remove(file.idx_path()));
    }

    // Reaching here proves that the noexcept destructor did not terminate the
    // process. A later open can reconstruct the missing index from the log.
    zidanedb::Database recovered{file.path(), 4};
    CHECK(recovered.get("player") == "Zidane");
}
