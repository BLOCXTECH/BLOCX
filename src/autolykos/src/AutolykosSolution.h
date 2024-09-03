// Copyright (c) 2017-2024 The ERGO developers
// Copyright (c) 2023-2024 The BLOCX developers

#ifndef AUTOLYKOS_SOLUTION_H
#define AUTOLYKOS_SOLUTION_H

#include "includes.h"

struct AutolykosSolution {
    std::vector<uint8_t> n;

    AutolykosSolution(const std::vector<uint8_t>& n)
        :  n(n) {}

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

