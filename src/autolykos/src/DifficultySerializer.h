#ifndef DIFFICULTY_SERIALIZER_H
#define DIFFICULTY_SERIALIZER_H

#include "includes.h"

boost::multiprecision::cpp_int decodeCompactBits(uint32_t compact);
std::uint64_t encodeCompactBits(const boost::multiprecision::cpp_int& requiredDifficulty);

void serialize(uint32_t obj, Writer& w);


#endif // DIFFICULTY_SERIALIZER_H

