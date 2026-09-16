#include <catch2/catch_test_macros.hpp>

#include "utils.h"

#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

TEST_CASE("serialized string sizes include a prefix and use a wide type", "[utils][regression]") {
    using Size = decltype(zidanedb::utils::string_size(std::string_view{}));
    CHECK(std::numeric_limits<Size>::max() > std::numeric_limits<std::uint32_t>::max());
    CHECK(zidanedb::utils::string_size("") == sizeof(std::uint32_t));
    CHECK(zidanedb::utils::string_size("hello") == sizeof(std::uint32_t) + 5);
}

TEST_CASE("string helpers round-trip boundary lengths and binary data",
          "[utils][limits][regression]") {
    std::uint32_t limit = 4;
    std::string value;

    SECTION("empty string with a zero limit") { limit = 0; }
    SECTION("length exactly equals the limit") { value = "abcd"; }
    SECTION("embedded null bytes are preserved") { value = std::string{"a\0bc", 4}; }

    std::stringstream stream;
    zidanedb::utils::write_string(stream, value, limit);
    CHECK(stream.str().size() == sizeof(std::uint32_t) + value.size());

    std::string result = "previous contents";
    REQUIRE(zidanedb::utils::read_string(stream, result, limit));
    CHECK(result == value);
}

TEST_CASE("write_string rejects oversized input before writing anything",
          "[utils][limits][regression]") {
    std::ostringstream stream;
    stream << "existing bytes";
    const auto before = stream.str();

    REQUIRE_THROWS_AS(zidanedb::utils::write_string(stream, "abcde", 4), std::length_error);
    CHECK(stream.str() == before);
}

TEST_CASE("read_string rejects oversized lengths before resizing", "[utils][limits][regression]") {
    std::stringstream stream;
    zidanedb::utils::write_uint32(stream, 5);
    stream.write("hello", 5);
    std::string result = "unchanged";

    REQUIRE_FALSE(zidanedb::utils::read_string(stream, result, 4));
    CHECK(result == "unchanged");
    // Only the prefix should have been consumed, not the payload.
    CHECK(stream.tellg() == std::streampos{sizeof(std::uint32_t)});
}

TEST_CASE("read_string rejects truncated prefixes and payloads", "[utils][regression]") {
    std::stringstream stream;
    zidanedb::utils::write_uint32(stream, 4);

    SECTION("length prefix is incomplete") {
        stream.str(stream.str().substr(0, sizeof(std::uint32_t) - 1));
    }
    SECTION("payload is shorter than its declared length") { stream.write("ab", 2); }

    std::string result;
    CHECK_FALSE(zidanedb::utils::read_string(stream, result, 4));
}
