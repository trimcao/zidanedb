#ifndef ZIDANEDB_DATABASE_UTILS_H
#define ZIDANEDB_DATABASE_UTILS_H

#include <cppcodec/base64_rfc4648.hpp>
#include <string>

using base64 = cppcodec::base64_rfc4648;

inline std::string encode_base64(const std::string& input)
{
    return base64::encode(input);
}

inline std::string decode_base64(const std::string& input)
{
    return base64::decode<std::string>(input);
}

#endif