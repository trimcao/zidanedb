#include "utils.h"
#include <cstdint>
#include <ios>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace zidanedb::utils {

void write_string(std::ostream& stream, const std::string& s, std::uint32_t max_length) {
    if (s.size() > max_length) {
        throw std::length_error("String is too large to serialize");
    }
    const std::uint32_t length = static_cast<std::uint32_t>(s.size());
    stream.write(reinterpret_cast<const char*>(&length), sizeof(length));
    stream.write(s.data(), static_cast<std::streamsize>(s.size()));
}

bool read_string(std::istream& stream, std::string& result, std::uint32_t max_length) {
    std::uint32_t length = {};
    if (!stream.read(reinterpret_cast<char*>(&length), sizeof(length))) {
        return false;
    }

    if (length > max_length) {
        return false;
    }

    result.resize(length);

    if (!stream.read(result.data(), static_cast<std::streamsize>(length))) {
        return false;
    }

    return true;
}

bool read_uint64(std::istream& stream, std::uint64_t& result) {
    if (!stream.read(reinterpret_cast<char*>(&result), sizeof(std::uint64_t))) {
        return false;
    }

    return true;
}

void write_uint64(std::ostream& stream, const std::uint64_t n) {
    stream.write(reinterpret_cast<const char*>(&n), sizeof(n));
}

bool read_uint32(std::istream& stream, std::uint32_t& result) {
    if (!stream.read(reinterpret_cast<char*>(&result), sizeof(std::uint32_t))) {
        return false;
    }

    return true;
}

void write_uint32(std::ostream& stream, const std::uint32_t n) {
    stream.write(reinterpret_cast<const char*>(&n), sizeof(n));
}

bool read_uint8(std::istream& stream, std::uint8_t& result) {
    if (!stream.read(reinterpret_cast<char*>(&result), sizeof(std::uint8_t))) {
        return false;
    }

    return true;
}

void write_uint8(std::ostream& stream, const std::uint8_t n) {
    stream.write(reinterpret_cast<const char*>(&n), sizeof(n));
}

std::uint64_t fnv1a(std::string_view key) {
    std::uint64_t hash = 14695981039346656037ULL;

    for (unsigned char c : key) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }

    return hash;
}

// return the number of bytes used by write_string() function above
std::uint64_t string_size(std::string_view s) { return sizeof(std::uint32_t) + s.size(); }

} // namespace zidanedb::utils