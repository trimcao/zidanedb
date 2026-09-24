#include <catch2/catch_test_macros.hpp>

#include "record.h"

#include <crc32c/crc32c.h>

#include <cstdint>
#include <sstream>
#include <string>

namespace {

void append_uint32_little_endian(std::string& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<char>(value));
    bytes.push_back(static_cast<char>(value >> 8));
    bytes.push_back(static_cast<char>(value >> 16));
    bytes.push_back(static_cast<char>(value >> 24));
}

std::string record_bytes_without_checksum(const zidanedb::Record& record) {
    std::string bytes;

    bytes.push_back(static_cast<char>(record.type));
    append_uint32_little_endian(bytes, static_cast<std::uint32_t>(record.key.size()));
    bytes.append(record.key);
    append_uint32_little_endian(bytes, static_cast<std::uint32_t>(record.value.size()));
    bytes.append(record.value);

    return bytes;
}

std::string valid_record_bytes(const zidanedb::Record& record) {
    std::string bytes = record_bytes_without_checksum(record);
    const std::uint32_t checksum = crc32c::Crc32c(bytes);
    append_uint32_little_endian(bytes, checksum);
    return bytes;
}

} // namespace

TEST_CASE("write_record appends CRC32C of all preceding record bytes",
          "[record][checksum][regression]") {
    const zidanedb::Record record{zidanedb::RecordType::Put, "key", "value"};
    std::ostringstream stream;

    zidanedb::write_record(stream, record);

    CHECK(stream.str() == valid_record_bytes(record));
}

TEST_CASE("read_record accepts a record with a valid CRC32C checksum",
          "[record][checksum][regression]") {
    const zidanedb::Record expected{zidanedb::RecordType::Put, "key", "value"};
    std::istringstream stream{valid_record_bytes(expected)};
    zidanedb::Record actual{};

    REQUIRE(zidanedb::read_record(stream, actual) == zidanedb::RecordReadStatus::Success);
    CHECK(actual.type == expected.type);
    CHECK(actual.key == expected.key);
    CHECK(actual.value == expected.value);
}

TEST_CASE("read_record rejects a record when a value byte is corrupted",
          "[record][checksum][regression]") {
    const zidanedb::Record original{zidanedb::RecordType::Put, "key", "value"};
    std::string bytes = valid_record_bytes(original);

    const std::size_t value_offset =
        sizeof(std::uint8_t) + sizeof(std::uint32_t) + original.key.size() + sizeof(std::uint32_t);
    REQUIRE(value_offset < bytes.size() - sizeof(std::uint32_t));
    bytes[value_offset] = 'V';

    std::istringstream stream{bytes};
    zidanedb::Record result{};
    CHECK_FALSE(zidanedb::read_record(stream, result) == zidanedb::RecordReadStatus::Success);
}

TEST_CASE("read_record rejects a record when its type byte is corrupted",
          "[record][checksum][regression]") {
    const zidanedb::Record original{zidanedb::RecordType::Put, "key", "value"};
    std::string bytes = valid_record_bytes(original);

    bytes[0] = static_cast<char>(zidanedb::RecordType::Delete);

    std::istringstream stream{bytes};
    zidanedb::Record result{};
    CHECK_FALSE(zidanedb::read_record(stream, result) == zidanedb::RecordReadStatus::Success);
}

TEST_CASE("read_record rejects a record when its key length is corrupted",
          "[record][checksum][regression]") {
    const zidanedb::Record original{zidanedb::RecordType::Put, "key", "value"};
    std::string bytes = valid_record_bytes(original);

    const std::size_t key_length_offset = sizeof(std::uint8_t);
    bytes[key_length_offset] = static_cast<char>(original.key.size() - 1);

    std::istringstream stream{bytes};
    zidanedb::Record result{};
    CHECK_FALSE(zidanedb::read_record(stream, result) == zidanedb::RecordReadStatus::Success);
}

TEST_CASE("read_record rejects a record when a key byte is corrupted",
          "[record][checksum][regression]") {
    const zidanedb::Record original{zidanedb::RecordType::Put, "key", "value"};
    std::string bytes = valid_record_bytes(original);

    const std::size_t key_offset = sizeof(std::uint8_t) + sizeof(std::uint32_t);
    bytes[key_offset] = 'K';

    std::istringstream stream{bytes};
    zidanedb::Record result{};
    CHECK_FALSE(zidanedb::read_record(stream, result) == zidanedb::RecordReadStatus::Success);
}

TEST_CASE("read_record rejects a record when its value length is corrupted",
          "[record][checksum][regression]") {
    const zidanedb::Record original{zidanedb::RecordType::Put, "key", "value"};
    std::string bytes = valid_record_bytes(original);

    const std::size_t value_length_offset =
        sizeof(std::uint8_t) + sizeof(std::uint32_t) + original.key.size();
    bytes[value_length_offset] = static_cast<char>(original.value.size() - 1);

    std::istringstream stream{bytes};
    zidanedb::Record result{};
    CHECK_FALSE(zidanedb::read_record(stream, result) == zidanedb::RecordReadStatus::Success);
}

TEST_CASE("read_record rejects a record when its stored checksum is corrupted",
          "[record][checksum][regression]") {
    const zidanedb::Record original{zidanedb::RecordType::Put, "key", "value"};
    std::string bytes = valid_record_bytes(original);

    const std::size_t checksum_offset = bytes.size() - sizeof(std::uint32_t);
    bytes[checksum_offset] ^= 0x01;

    std::istringstream stream{bytes};
    zidanedb::Record result{};
    CHECK_FALSE(zidanedb::read_record(stream, result) == zidanedb::RecordReadStatus::Success);
}

TEST_CASE("read_record rejects every truncated prefix of a valid record",
          "[record][checksum][truncation][regression]") {
    const zidanedb::Record original{zidanedb::RecordType::Put, "key", "value"};
    const std::string complete = valid_record_bytes(original);

    for (std::size_t bytes_to_keep = 0; bytes_to_keep < complete.size(); ++bytes_to_keep) {
        CAPTURE(bytes_to_keep);
        std::istringstream stream{complete.substr(0, bytes_to_keep)};
        zidanedb::Record result{};

        CHECK_FALSE(zidanedb::read_record(stream, result) == zidanedb::RecordReadStatus::Success);
    }
}

TEST_CASE("record checksums support empty fields and embedded null bytes",
          "[record][checksum][regression]") {
    zidanedb::Record expected{zidanedb::RecordType::Put, "", ""};

    SECTION("empty delete record") { expected.type = zidanedb::RecordType::Delete; }

    SECTION("embedded null bytes") {
        expected.key = std::string{"k\0y", 3};
        expected.value = std::string{"v\0lue", 5};
    }

    std::ostringstream output;
    zidanedb::write_record(output, expected);

    std::istringstream input{output.str()};
    zidanedb::Record actual{};
    REQUIRE(zidanedb::read_record(input, actual) == zidanedb::RecordReadStatus::Success);
    CHECK(actual.type == expected.type);
    CHECK(actual.key == expected.key);
    CHECK(actual.value == expected.value);
}
