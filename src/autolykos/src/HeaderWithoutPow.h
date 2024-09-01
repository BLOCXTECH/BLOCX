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
    Digest32 ADProofsRoot;
    ADDigest stateRoot;
    Digest32 transactionsRoot;
    Timestamp timestamp;
    uint32_t nBits;
    uint32_t height;
    Digest32 extensionRoot;
    std::array<uint8_t, 3> votes;

    HeaderWithoutPow(Version version, const ModifierId& parentId, const Digest32& ADProofsRoot, const ADDigest& stateRoot, const Digest32& transactionsRoot,
                     Timestamp timestamp, uint32_t nBits, uint32_t height, const Digest32& extensionRoot, const std::array<uint8_t, 3>& votes)
                    : version(version), parentId(parentId), ADProofsRoot(ADProofsRoot), stateRoot(stateRoot), transactionsRoot(transactionsRoot), 
                      timestamp(timestamp), nBits(nBits), height(height), extensionRoot(extensionRoot), votes(votes) {}
    
    // Method to convert to Header, definition to be provided later
    Header toHeader(const AutolykosSolution& powSolution, const std::optional<int>& headerSize = std::nullopt) const {
        return Header(version, parentId, ADProofsRoot, stateRoot, transactionsRoot, timestamp,
                      nBits, height, extensionRoot, powSolution, votes, headerSize);
    }
};

#endif // HEADERWITHOUTPOW_H

