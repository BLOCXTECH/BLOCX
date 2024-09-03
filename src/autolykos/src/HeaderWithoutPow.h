// Copyright (c) 2017-2024 The ERGO developers
// Copyright (c) 2023-2024 The BLOCX developers

#ifndef HEADERWITHOUTPOW_H
#define HEADERWITHOUTPOW_H

#include "includes.h"

// Placeholder types, define them as per your requirements
using Digest32 = std::array<uint8_t, 32>;
using ModifierId = std::vector<uint8_t>;
using ADDigest = std::vector<uint8_t>;
using Timestamp = uint64_t;
using Version = uint8_t;

class HeaderWithoutPow {
public:
    Version version;
    ModifierId parentId;
    Digest32 transactionsRoot;
    Timestamp timestamp;
    uint32_t nBits;
    uint32_t height;

    HeaderWithoutPow(Version version, const ModifierId& parentId, const Digest32& transactionsRoot,
                     Timestamp timestamp, uint32_t nBits, uint32_t height)
                    : version(version), parentId(parentId), transactionsRoot(transactionsRoot), 
                      timestamp(timestamp), nBits(nBits), height(height) {}
    
    // Method to convert to Header, definition to be provided later
    Header toHeader(const AutolykosSolution& powSolution, const std::optional<int>& headerSize = std::nullopt) const {
        return Header(version, parentId, transactionsRoot, timestamp,
                      nBits, height, powSolution, headerSize);
    }
};

#endif // HEADERWITHOUTPOW_H

