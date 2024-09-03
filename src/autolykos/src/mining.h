// Copyright (c) 2017-2024 The ERGO developers
// Copyright (c) 2023-2024 The BLOCX developers

#ifndef MINING_H
#define MINING_H

#include "includes.h"

namespace bmp = boost::multiprecision;


std::vector<uint8_t> hash(const std::vector<uint8_t>& in); // Wrapper for Blake2b-256 hash function
bmp::cpp_int toBigInt(const std::vector<uint8_t>& data);


#endif // MINING_H

