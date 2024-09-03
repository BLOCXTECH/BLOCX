// Copyright (c) 2017-2024 The ERGO developers
// Copyright (c) 2023-2024 The BLOCX developers

#ifndef HEADER_H
#define HEADER_H

#include "includes.h"

using Digest32 = std::array<uint8_t, 32>; // Assuming Digest32 and other types are 32 bytes
using ModifierId = std::vector<uint8_t>; // Placeholder for actual type
using ADDigest = std::vector<uint8_t>; // Placeholder for actual type
using Timestamp = uint64_t; // Assuming timestamp is uint64_t
using Version = uint8_t; // Assuming version is uint8_t
using BigInt = boost::multiprecision::cpp_int; 

class Header {
public:
    Version version;
    ModifierId parentId;
    Digest32 transactionsRoot;
    Timestamp timestamp;
    uint32_t nBits;
    int height;
    AutolykosSolution powSolution;
    std::array<uint8_t, 3> votes;
    std::optional<int> sizeOpt;
    
    // Method to copy powSolution
    void copy(const AutolykosSolution& solution) {
        powSolution = solution;
    }

    /**
     * Block version during mainnet launch
     */
    static const Version InitialVersion = 1;

    Header(Version version,
           const ModifierId& parentId,
           const Digest32& transactionsRoot,
           Timestamp timestamp,
           uint32_t nBits,
           int height,
           const AutolykosSolution& powSolution,
           const std::optional<int>& sizeOpt = std::nullopt)
        : version(version),
          parentId(parentId),
          transactionsRoot(transactionsRoot),
          timestamp(timestamp),
          nBits(nBits),
          height(height),
          powSolution(powSolution),
          sizeOpt(sizeOpt){}
};


#endif // HEADER_H