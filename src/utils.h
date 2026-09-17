#ifndef ZIDANEDB_DATABASE_UTILS_H
#define ZIDANEDB_DATABASE_UTILS_H

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

namespace zidanedb::utils {

void write_string(std::ostream& stream, const std::string& s, std::uint32_t max_length);
bool read_string(std::istream& stream, std::string& result, std::uint32_t max_length);
bool read_uint64(std::istream& stream, std::uint64_t& result);
void write_uint64(std::ostream& stream, const std::uint64_t n);
bool read_uint32(std::istream& stream, std::uint32_t& result);
void write_uint32(std::ostream& stream, const std::uint32_t n);
bool read_uint8(std::istream& stream, std::uint8_t& result);
void write_uint8(std::ostream& stream, const std::uint8_t n);
std::uint64_t fnv1a(std::string_view key);
std::uint64_t string_size(std::string_view s);

} // namespace zidanedb::utils

#endif