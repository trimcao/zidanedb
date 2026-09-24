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

RecordReadStatus read_record(std::istream& stream, Record& record) {
    Record candidate;

    std::uint32_t checksum = 0;
    utils::ReadStatus read_status{};

    std::uint8_t type{};
    if ((read_status = utils::read_uint8(stream, type)) != utils::ReadStatus::Success) {
        return RecordReadStatus::EndOfFile;
    }
    candidate.type = static_cast<RecordType>(type);
    if (candidate.type != RecordType::Put && candidate.type != RecordType::Delete) {
        return RecordReadStatus::InvalidType;
    }
    checksum = extend_checksum_record_type(checksum, candidate.type);

    // note: we already check the stored string length inside read_string()
    // so at least we will reject a corrupted length that's too big.
    if ((read_status = utils::read_string(stream, candidate.key, MAX_KEY_SIZE)) !=
        utils::ReadStatus::Success) {
        if (read_status == utils::ReadStatus::InvalidLength) {
            return RecordReadStatus::InvalidLength;
        } else {
            return RecordReadStatus::Truncated;
        }
    }
    checksum = extend_checksum_uint32(checksum, candidate.key.size());
    checksum = extend_checksum_string(checksum, candidate.key);

    if ((read_status = utils::read_string(stream, candidate.value, MAX_VALUE_SIZE)) !=
        utils::ReadStatus::Success) {
        if (read_status == utils::ReadStatus::InvalidLength) {
            return RecordReadStatus::InvalidLength;
        } else {
            return RecordReadStatus::Truncated;
        }
    }
    checksum = extend_checksum_uint32(checksum, candidate.value.size());
    checksum = extend_checksum_string(checksum, candidate.value);

    uint32_t stored_checksum;
    if ((read_status = utils::read_uint32(stream, stored_checksum)) != utils::ReadStatus::Success) {
        return RecordReadStatus::Truncated;
    }
    if (checksum != stored_checksum) {
        return RecordReadStatus::ChecksumMismatch;
    }

    record = std::move(candidate);
    return RecordReadStatus::Success;
}

void write_record(std::ostream& stream, const Record& record) {
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