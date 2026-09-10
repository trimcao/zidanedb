#ifndef ZIDANEDB_DATABASE_UTILS_H
#define ZIDANEDB_DATABASE_UTILS_H

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

namespace zidanedb::utils {

void write_string(std::ostream& stream, const std::string& s);
bool read_string(std::istream& stream, std::string& result);
bool read_uint64(std::istream& stream, std::uint64_t& result);
void write_uint64(std::ostream& stream, const std::uint64_t n);
bool read_uint32(std::istream& stream, std::uint32_t& result);
void write_uint32(std::ostream& stream, const std::uint32_t n);
std::uint64_t fnv1a(std::string_view key);
std::uint32_t string_size(std::string_view s);

} // namespace zidanedb::utils

#endif