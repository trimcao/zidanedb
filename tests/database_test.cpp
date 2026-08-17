#include <catch2/catch_test_macros.hpp>

#include "zidanedb/database.h"

TEST_CASE("get returns no value for a missing key")
{
    zidanedb::Database db{"test.zdb"};
    const auto result = db.get("missing");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("put stores a value")
{
    zidanedb::Database db{"test.zdb"};
    db.put("player", "Bellingham");
    const auto result = db.get("player");
    REQUIRE(result.has_value());
    REQUIRE(*result == "Bellingham");
}

TEST_CASE("put replaces an existing value")
{
    zidanedb::Database db{"test.zdb"};
    db.put("player", "Bellingham");
    db.put("player", "Ronaldo");
    const auto result = db.get("player");
    REQUIRE(result.has_value());
    REQUIRE(*result == "Ronaldo");
}

TEST_CASE("erase removes an existing key")
{
    zidanedb::Database db{"test.zdb"};
    db.put("player", "Bellingham");
    const auto existed = db.erase("player");
    REQUIRE(existed);
    REQUIRE_FALSE(db.get("player").has_value());
}

TEST_CASE("erase returns false for a missing key")
{
    zidanedb::Database db{"test.db"};
    REQUIRE_FALSE(db.erase("missing"));
}