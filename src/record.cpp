#include "record.h"
#include "constants.h"
#include "utils.h"
#include <array>
#include <crc32c/crc32c.h>
#include <cstdint>

namespace {

std::uint32_t extend_checksum_string(std::uint32_t checksum, const std::string& piece) {
    return crc32c::Extend(checksum, reinterpret_cast<const std::uint8_t*>(piece.data()),
                          piece.size());
}

std::uint32_t extend_checksum_record_type(std::uint32_t checksum, zidanedb::RecordType type) {
    const auto encoded_type = static_cast<std::uint8_t>(type);
    return crc32c::Extend(checksum, &encoded_type, sizeof(encoded_type));
}

std::uint32_t extend_checksum_uint32(std::uint32_t checksum, std::uint32_t number) {
    // make sure the order is little-endian
    const std::array<std::uint8_t, 4> bytes{
        static_cast<std::uint8_t>(number), static_cast<std::uint8_t>(number >> 8),
        static_cast<std::uint8_t>(number >> 16), static_cast<std::uint8_t>(number >> 24)};
    return crc32c::Extend(checksum, bytes.data(), bytes.size());
}

} // namespace

namespace zidanedb {

bool read_record(std::istream& stream, Record& record) {
    std::uint32_t checksum = 0;

    std::uint8_t type{};
    if (!utils::read_uint8(stream, type)) {
        return false;
    }
    record.type = static_cast<RecordType>(type);
    checksum = extend_checksum_record_type(checksum, record.type);

    if (!utils::read_string(stream, record.key, MAX_KEY_SIZE)) {
        return false;
    }
    checksum = extend_checksum_uint32(checksum, record.key.size());
    checksum = extend_checksum_string(checksum, record.key);

    if (!utils::read_string(stream, record.value, MAX_VALUE_SIZE)) {
        return false;
    }
    checksum = extend_checksum_uint32(checksum, record.value.size());
    checksum = extend_checksum_string(checksum, record.value);

    if (!utils::read_uint32(stream, record.crc32c)) {
        return false;
    }
    if (checksum != record.crc32c) {
        return false;
    }

    return true;
}

void write_record(std::ostream& stream, Record& record) {
    std::uint32_t checksum = 0;

    utils::write_uint8(stream, static_cast<std::uint8_t>(record.type));
    checksum = extend_checksum_record_type(checksum, record.type);

    utils::write_string(stream, record.key, MAX_KEY_SIZE);
    checksum = extend_checksum_uint32(checksum, record.key.size());
    checksum = extend_checksum_string(checksum, record.key);

    utils::write_string(stream, record.value, MAX_VALUE_SIZE);
    checksum = extend_checksum_uint32(checksum, record.value.size());
    checksum = extend_checksum_string(checksum, record.value);

    utils::write_uint32(stream, checksum);
}

} // namespace zidanedb