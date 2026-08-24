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
        : path_{
            // note: For filesystem paths, / is overloaded
            // to mean 'join these path components.'
            std::filesystem::temp_directory_path() /
            filename
        }
    {
        remove();
    }

    ~TemporaryDatabaseFile()
    {
        remove();
    }

    // note: The trailing const promises that calling path()
    // does not modify the TemporaryDatabaseFile object
    const std::filesystem::path& path() const
    {
        return path_;
    }

private:
    std::filesystem::path path_;

    void remove()
    {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }
};

} // namespace


TEST_CASE("get returns no value for a missing key")
{
    TemporaryDatabaseFile file{
        "get-missing-key.zdb"
    };
    zidanedb::Database db{file.path()};
    const auto result = db.get("missing");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("put stores a value")
{
    TemporaryDatabaseFile file{
        "put.zdb"
    };
    zidanedb::Database db{file.path()};
    db.put("player", "Bellingham");
    const auto result = db.get("player");
    REQUIRE(result.has_value());
    REQUIRE(*result == "Bellingham");
}

TEST_CASE("put replaces an existing value")
{
    TemporaryDatabaseFile file{
        "put-existing.zdb"
    };
    zidanedb::Database db{file.path()};
    db.put("player", "Bellingham");
    db.put("player", "Ronaldo");
    const auto result = db.get("player");
    REQUIRE(result.has_value());
    REQUIRE(*result == "Ronaldo");
}

TEST_CASE("erase removes an existing key")
{
    TemporaryDatabaseFile file{
        "erase.zdb"
    };
    zidanedb::Database db{file.path()};
    db.put("player", "Bellingham");
    const auto existed = db.erase("player");
    REQUIRE(existed);
    REQUIRE_FALSE(db.get("player").has_value());
}

TEST_CASE("erase returns false for a missing key")
{
    TemporaryDatabaseFile file{
        "erase-missing-key.zdb"
    };
    zidanedb::Database db{file.path()};
    REQUIRE(db.erase("missing"));
}

TEST_CASE("put persists values after reopening")
{
    TemporaryDatabaseFile file{
        "zidanedb-put-persistence-test.zdb"
    };

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

TEST_CASE("replaced values remain replaced after reopening")
{
    TemporaryDatabaseFile file{
        "zidanedb-replace-persistence-test.zdb"
    };

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

TEST_CASE("erased values remain erased after reopening")
{
    TemporaryDatabaseFile file{
        "zidanedb-erase-persistence-test.zdb"
    };

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

TEST_CASE("multi-line value should work")
{
    TemporaryDatabaseFile file{
        "zidanedb-multiline-persistence-test.zdb"
    };

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