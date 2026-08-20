#include "utils.h"
#include <string_view>

std::string random_string(
    std::mt19937_64& random_engine,
    std::size_t length)
{
    constexpr std::string_view characters{
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789"
    };

    std::uniform_int_distribution<std::size_t> pick{
        0,
        characters.size() - 1
    };

    std::string result;
    result.reserve(length);

    for (std::size_t index = 0; index < length; ++index) {
        result.push_back(characters[pick(random_engine)]);
    }

    return result;
}