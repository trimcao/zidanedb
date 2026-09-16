#ifndef ZIDANEDB_CONSTANTS_H
#define ZIDANEDB_CONSTANTS_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace zidanedb {

const std::uint32_t MAX_KEY_SIZE = 1'024;
const std::uint32_t MAX_VALUE_SIZE = 16 * 1'024 * 1'024;

const std::string INDEX_MAGIC = "ZIDANEDBINDEX026";
const std::uint32_t INDEX_VERSION = 1;

} // namespace zidanedb

#endif
