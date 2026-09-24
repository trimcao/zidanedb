#include <catch2/catch_test_macros.hpp>

#include "utils.h"

#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

using zidanedb::utils::ReadStatus;

TEST_CASE("uint32 helpers use little-endian byte order", "[utils][endian][regression]") {
    constexpr std::uint32_t value = 0x89ABCDEFU;
    const std::string encoded{static_cast<char>(0xEF), static_cast<char>(0xCD),
                              static_cast<char>(0xAB), static_cast<char>(0x89)};

    SECTION("writer produces the specified bytes") {
        std::ostringstream stream;
        zidanedb::utils::write_uint32(stream, value);

        CHECK(stream.str() == encoded);
    }

    SECTION("reader accepts independently specified bytes") {
        std::istringstream stream{encoded};
        std::uint32_t result{};

        REQUIRE(zidanedb::utils::read_uint32(stream, result) == ReadStatus::Success);
        CHECK(result == value);
    }
}

TEST_CASE("uint64 helpers use little-endian byte order", "[utils][endian][regression]") {
    constexpr std::uint64_t value = UINT64_C(0xFEDCBA9876543210);
    const std::string encoded{
        static_cast<char>(0x10), static_cast<char>(0x32), static_cast<char>(0x54),
        static_cast<char>(0x76), static_cast<char>(0x98), static_cast<char>(0xBA),
        static_cast<char>(0xDC), static_cast<char>(0xFE),
    };

    SECTION("writer produces the specified bytes") {
        std::ostringstream stream;
        zidanedb::utils::write_uint64(stream, value);

        CHECK(stream.str() == encoded);
    }

    SECTION("reader accepts independently specified bytes") {
        std::istringstream stream{encoded};
        std::uint64_t result{};

        REQUIRE(zidanedb::utils::read_uint64(stream, result) == ReadStatus::Success);
        CHECK(result == value);
    }
}

TEST_CASE("unsigned integer readers reject every truncated byte sequence",
          "[utils][endian][truncation][regression]") {
    SECTION("uint32") {
        const std::string complete{static_cast<char>(0xEF), static_cast<char>(0xCD),
                                   static_cast<char>(0xAB), static_cast<char>(0x89)};

        for (std::size_t bytes_to_keep = 0; bytes_to_keep < complete.size(); ++bytes_to_keep) {
            CAPTURE(bytes_to_keep);
            std::istringstream stream{complete.substr(0, bytes_to_keep)};
            std::uint32_t result{};

            CHECK_FALSE(zidanedb::utils::read_uint32(stream, result) == ReadStatus::Success);
        }
    }

    SECTION("uint64") {
        const std::string complete{
            static_cast<char>(0x10), static_cast<char>(0x32), static_cast<char>(0x54),
            static_cast<char>(0x76), static_cast<char>(0x98), static_cast<char>(0xBA),
            static_cast<char>(0xDC), static_cast<char>(0xFE),
        };

        for (std::size_t bytes_to_keep = 0; bytes_to_keep < complete.size(); ++bytes_to_keep) {
            CAPTURE(bytes_to_keep);
            std::istringstream stream{complete.substr(0, bytes_to_keep)};
            std::uint64_t result{};

            CHECK_FALSE(zidanedb::utils::read_uint64(stream, result) == ReadStatus::Success);
        }
    }
}

TEST_CASE("string helpers use a little-endian length prefix", "[utils][endian][regression]") {
    const std::string encoded{static_cast<char>(0x03),
                              static_cast<char>(0x00),
                              static_cast<char>(0x00),
                              static_cast<char>(0x00),
                              'a',
                              'b',
                              'c'};

    SECTION("writer produces the specified bytes") {
        std::ostringstream stream;
        zidanedb::utils::write_string(stream, "abc", 3);

        CHECK(stream.str() == encoded);
    }

    SECTION("reader accepts independently specified bytes") {
        std::istringstream stream{encoded};
        std::string result;

        REQUIRE(zidanedb::utils::read_string(stream, result, 3) == ReadStatus::Success);
        CHECK(result == "abc");
    }
}

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
    REQUIRE(zidanedb::utils::read_string(stream, result, limit) == ReadStatus::Success);
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

    REQUIRE_FALSE(zidanedb::utils::read_string(stream, result, 4) == ReadStatus::Success);
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
    CHECK_FALSE(zidanedb::utils::read_string(stream, result, 4) == ReadStatus::Success);
}
