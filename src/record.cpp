#include "record.h"
#include "constants.h"
#include "utils.h"
#include <cstdint>

namespace zidanedb {

bool read_record(std::istream& stream, Record& record) {
    std::uint8_t type{};
    if (!utils::read_uint8(stream, type)) {
        return false;
    }
    record.type = static_cast<RecordType>(type);

    if (!utils::read_string(stream, record.key, MAX_KEY_SIZE)) {
        return false;
    }

    if (!utils::read_string(stream, record.value, MAX_VALUE_SIZE)) {
        return false;
    }

    // TODO: compute the checksum
    std::uint32_t checksum = 1;
    if (!utils::read_uint32(stream, record.crc32c)) {
        return false;
    }
    if (checksum != record.crc32c) {
        return false;
    }

    return true;
}

void write_record(std::ostream& stream, Record& record) {
    // TODO: compute the checksum
    std::uint32_t checksum = 1;

    utils::write_uint8(stream, static_cast<std::uint8_t>(record.type));
    utils::write_string(stream, record.key, MAX_KEY_SIZE);
    utils::write_string(stream, record.value, MAX_VALUE_SIZE);
    utils::write_uint32(stream, checksum);
}

} // namespace zidanedb