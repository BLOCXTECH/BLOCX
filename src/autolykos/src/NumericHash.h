#ifndef NUMERIC_HASH_HPP
#define NUMERIC_HASH_HPP

#include "includes.h"

class NumericHash {
public:
    explicit NumericHash(const boost::multiprecision::cpp_int& q);

    boost::multiprecision::cpp_int hash(const std::vector<uint8_t>& input);

private:
    boost::multiprecision::cpp_int q;
    boost::multiprecision::cpp_int validRange;
};

#endif // NUMERIC_HASH_HPP

