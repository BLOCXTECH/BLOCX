// Copyright (c) 2017-2024 The ERGO developers
// Copyright (c) 2023-2024 The BLOCX developers

#include "includes.h"
#include "HSerializer.h"

uint256 reverseUint256(const uint256& value) {
    uint256 reversed_value = value;
    std::reverse(reversed_value.begin(), reversed_value.end());
    return reversed_value;
}

void HeaderSerializer::serializeWithoutPow(const HeaderWithoutPow& h, Writer& w) {
    int32_t nVersion{0};
    uint256 previousHash(h.parentId);
    uint256 hashMerkleRoot;
    uint32_t nTime{0};
    uint32_t nBits{0};
    uint32_t aHeight{0};

    nVersion = static_cast<int32_t>(h.version);
    std::memcpy(&hashMerkleRoot, h.transactionsRoot.data(), h.transactionsRoot.size());
    nTime = static_cast<uint32_t>(h.timestamp);
    nBits = h.nBits;

    HSerializer hserializer(nVersion, reverseUint256(previousHash), reverseUint256(hashMerkleRoot), nTime, nBits, h.height);
    hserializer.Serialize(w);
}

std::vector<uint8_t> HeaderSerializer::bytesWithoutPow(const HeaderWithoutPow& header) {
    Writer w;
    serializeWithoutPow(header, w);
    return w.result();
}

void HeaderSerializer::Serialize(const Header& h, Writer& w) {
    HeaderWithoutPow header(h.version, h.parentId, h.transactionsRoot, h.timestamp, h.nBits, h.height);
    AutolykosSolutionSerializer SolutionSerializer;

    serializeWithoutPow(header, w);
    SolutionSerializer.serialize(h.version, h.powSolution, w);
}

std::vector<uint8_t> HeaderSerializer::toBytes(const Header& obj) {
    Writer writer;
    Serialize(obj, writer);
    return writer.result();
}
