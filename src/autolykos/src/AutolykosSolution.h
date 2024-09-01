#ifndef AUTOLYKOS_SOLUTION_H
#define AUTOLYKOS_SOLUTION_H

#include "includes.h"

struct AutolykosSolution {
    secp256k1_pubkey pk;
    secp256k1_pubkey w;
    std::vector<uint8_t> n;
    boost::multiprecision::cpp_int d;

    AutolykosSolution(const secp256k1_pubkey& pk, const secp256k1_pubkey& w, const std::vector<uint8_t>& n, const boost::multiprecision::cpp_int& d)
        : pk(pk), w(w), n(n), d(d) {}

    std::vector<uint8_t> encodedPk() const {
        return groupElemToBytes(pk);
    }
};

class AutolykosV1SolutionSerializer {
public:
    void serialize(const AutolykosSolution& obj, Writer& w);
};

class AutolykosV2SolutionSerializer {
public:
    void serialize(const AutolykosSolution& obj, Writer& w);
};

class AutolykosSolutionSerializer {
public:
    void serializer(uint8_t blockVersion, const AutolykosSolution& solution, Writer& w);
    void serialize(uint8_t blockVersion, const AutolykosSolution& solution, Writer& w);
};

std::vector<uint8_t> asUnsignedByteArray(const bmp::cpp_int& bigInt);

#endif // AUTOLYKOS_SOLUTION_H

