#include "includes.h"

std::vector<uint8_t> asUnsignedByteArray(const bmp::cpp_int& bigInt) {
    std::vector<uint8_t> data;
    export_bits(bigInt, std::back_inserter(data), 8);
    return data;
}

void AutolykosV1SolutionSerializer::serialize(const AutolykosSolution& obj, Writer& w) {
    std::vector<uint8_t> dBytes = asUnsignedByteArray(obj.d);
    w.putBytes(groupElemToBytes(obj.pk));
    w.putBytes(groupElemToBytes(obj.w));
    assert(obj.n.size() == 8); // non-consensus check on prover side
    w.putBytes(obj.n);
    w.putUByte(dBytes.size());
    w.putBytes(dBytes);
}

void AutolykosV2SolutionSerializer::serialize(const AutolykosSolution& obj, Writer& w) {
    assert(obj.n.size() == 8); // non-consensus check on prover side
    w.putBytes(obj.n);
}

void AutolykosSolutionSerializer::serializer(uint8_t blockVersion, const AutolykosSolution& solution, Writer& w) {
    AutolykosV1SolutionSerializer v1Serializer;
    AutolykosV2SolutionSerializer v2Serializer;
    if (blockVersion == 1) {
        return v1Serializer.serialize(solution, w);
    } else {
        return v2Serializer.serialize(solution, w);
    }
}

void AutolykosSolutionSerializer::serialize(uint8_t blockVersion, const AutolykosSolution& solution, Writer& w) {
     serializer(blockVersion, solution, w);
}

