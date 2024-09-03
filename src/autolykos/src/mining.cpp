// Copyright (c) 2017-2024 The ERGO developers
// Copyright (c) 2023-2024 The BLOCX developers

#include "includes.h"

namespace bmp = boost::multiprecision;

// Group order, used in Autolykos V.1 for non-outsourceability,
// and also to obtain target in both Autolykos v1 and v2
const bmp::cpp_int q("115792089237316195423570985008687907852837564279074904382605163141518161494337");

/**
  * Convert byte array to unsigned integer
  * @param in - byte array
  * @return - unsigned integer
  */
bmp::cpp_int toBigInt(const std::vector<uint8_t>& data) {
    bmp::cpp_int result;
    import_bits(result, data.begin(), data.end());
    return result;
}

/**
 * Blake2b256 hash function invocation
 * @param in - input bit-string
 * @return - 256 bits (32 bytes) array
 */
std::vector<uint8_t> hash(const std::vector<uint8_t>& in) {
    return Blake2b256(in);
}
