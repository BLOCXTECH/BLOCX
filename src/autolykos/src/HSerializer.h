
#ifndef HSERIALIZER_H
#define HSERIALIZER_H

#include "includes.h"
#include "serialize.h"

class HSerializer
{
    int32_t nVersion{0};
    uint256 previousHash;
    uint256 hashMerkleRoot;
    uint32_t nTime{0};
    uint32_t nBits{0};
    uint32_t aHeight{0};

public:
    HSerializer(int32_t& version, const uint256& prevHash, const uint256& merkleRoot, uint32_t time, uint32_t bits, uint32_t height) :
        nVersion(version), previousHash(prevHash), hashMerkleRoot(merkleRoot), nTime(time), nBits(bits), aHeight(height) {}

    SERIALIZE_METHODS(HSerializer, obj)
    {
        READWRITE(obj.nVersion, obj.previousHash, obj.hashMerkleRoot, obj.nTime, obj.nBits, obj.aHeight);
    }
};

#endif // HSERIALIZER_H
