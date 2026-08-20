#ifndef MATRIX_UTILS_H
#define MATRIX_UTILS_H

#include <random>

std::string random_string(
    std::mt19937_64& random_engine,
    std::size_t length);

#endif //MATRIX_UTILS_H