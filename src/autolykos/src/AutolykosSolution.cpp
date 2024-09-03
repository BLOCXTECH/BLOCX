// Copyright (c) 2017-2024 The ERGO developers
// Copyright (c) 2023-2024 The BLOCX developers

#include "includes.h"

std::vector<uint8_t> asUnsignedByteArray(const bmp::cpp_int& bigInt) {
    std::vector<uint8_t> data;
    export_bits(bigInt, std::back_inserter(data), 8);
    return data;
}


void AutolykosV2SolutionSerializer::serialize(const AutolykosSolution& obj, Writer& w) {
    assert(obj.n.size() == 8); // non-consensus check on prover side
    w.putBytes(obj.n);
}

void AutolykosSolutionSerializer::serializer(uint8_t blockVersion, const AutolykosSolution& solution, Writer& w) {
    AutolykosV2SolutionSerializer ser;
    return ser.serialize(solution, w);
}

void AutolykosSolutionSerializer::serialize(uint8_t blockVersion, const AutolykosSolution& solution, Writer& w) {
     serializer(blockVersion, solution, w);
}

