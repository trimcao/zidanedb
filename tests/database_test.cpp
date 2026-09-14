#include <catch2/catch_test_macros.hpp>

#include "zidanedb/database.h"

#include <filesystem>
#include <string>
#include <system_error>

namespace {

/*
TemporaryDatabaseFile is a small test helper. Its job is:
1. Choose a path in the operating system’s temporary directory.
2. Ensure no old database exists at that path.
3. Let the test use the path.
4. Delete the test database when the test finishes.
*/
class TemporaryDatabaseFile {
  public:
    // note about the `explicit` keyword:
    // This prevents C++ from automatically converting a string
    // into a TemporaryDatabaseFile.
    explicit TemporaryDatabaseFile(const std::string& filename)
        : path_{// note: For filesystem paths, / is overloaded
                // to mean 'join these path components.'
                std::filesystem::temp_directory_path() / filename},
          idx_path_{path_} {

        idx_path_.replace_extension(".zidx");
        remove();
    }

    ~TemporaryDatabaseFile() { remove(); }

    // note: The trailing const promises that calling path()
    // does not modify the TemporaryDatabaseFile object
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

TEST_CASE("a new index entry persists after reopening") {
    TemporaryDatabaseFile file{"new-index-entry-persistence-test.zdb"};

    REQUIRE_FALSE(std::filesystem::exists(file.path()));
    REQUIRE_FALSE(std::filesystem::exists(file.idx_path()));

    {
        zidanedb::Database database{file.path()};
        database.put("new-key", "new-value");
    }

    REQUIRE(std::filesystem::exists(file.path()));
    REQUIRE(std::filesystem::exists(file.idx_path()));

    // This assertion directly reveals that nothing was appended.
    REQUIRE(std::filesystem::file_size(file.idx_path()) > 0);

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