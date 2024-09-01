#include "includes.h"

using namespace boost::multiprecision;

/**
  * One way cryptographic hash function that produces numbers in [0,q) range.
  * It calculates Blake2b256 hash of a provided input and checks whether the result is
  * in range from 0 to a maximum number divisible by q without remainder.
  * If yes, it returns the result mod q, otherwise make one more iteration using hash as an input.
  * This is done to ensure uniform distribution of the resulting numbers.
  */
NumericHash::NumericHash(const cpp_int& q) : q(q) {
    if (msb(q) > 256) {  // Use msb() to check the number of bits
        throw std::invalid_argument("We use 256 bit hash here");
    }
    cpp_int two = 2;
    validRange = (pow(two, 256) / q) * q;
}

cpp_int NumericHash::hash(const std::vector<uint8_t>& input) {
    std::vector<uint8_t> hashed = Blake2b256(input);
    cpp_int bi = cpp_int("0x" + boost::algorithm::hex(std::string(hashed.begin(), hashed.end())));
    if (bi < validRange) {
        return bi % q;
    } else {
        return hash(hashed);
    }
}
