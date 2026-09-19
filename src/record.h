#ifndef ZIDANEDB_RECORD_H
#define ZIDANEDB_RECORD_H

#include <cstdint>
#include <string>

namespace zidanedb {

// Records: [type:u8][key_length:u32][key_bytes][value_length:u32][value_bytes][crc32c:u32]
enum class RecordType : std::uint8_t { Put = 1, Delete = 2 };

struct Record {
    RecordType type;
    std::string key;
    std::string value;
};

bool read_record(std::istream& stream, Record& record);
void write_record(std::ostream& stream, const Record& record);

} // namespace zidanedb

#endif