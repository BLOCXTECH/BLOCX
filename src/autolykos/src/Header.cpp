#include "includes.h"

uint64_t Header::vectorToUint64(std::vector<uint8_t> n) const {
    if (n.size() < 8) {
        throw std::invalid_argument("Vector size must be at least 8 bytes to convert to uint32_t.");
    }

    uint64_t result = 0;
    for (size_t i = 0; i < 8; ++i) {
        result |= static_cast<uint64_t>(n[7 - i]) << (8 * i);
    }
    return result;
}

std::string Header::ToString() const
{
    std::stringstream s;
    uint256 previousHash(parentId);
    uint256 hashMerkleRoot;
    std::memcpy(&hashMerkleRoot, transactionsRoot.data(), transactionsRoot.size());
    uint64_t nonce = vectorToUint64(powSolution.n);
    s << strprintf("Header(version=0x%08x, parentId=%s, transactionsRoot=%s, timestamp=%u, nBits=%08x, height=%u, nNonce=%d)\n",
                   version,
                   previousHash.ToString(),
                   hashMerkleRoot.ToString(),
                   timestamp, nBits, height, nonce);
    return s.str();
}