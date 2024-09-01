#ifndef MINING_H
#define MINING_H

#include "includes.h"

namespace bmp = boost::multiprecision;

extern const bmp::cpp_int q;
extern NumericHash hashFn;

bmp::cpp_int hashModQ(const std::vector<uint8_t>& in);
std::vector<uint8_t> hash(const std::vector<uint8_t>& in); // Wrapper for Blake2b-256 hash function
bmp::cpp_int toBigInt(const std::vector<uint8_t>& data);
secp256k1_pubkey genPk(const bmp::cpp_int& privateKey);
std::vector<uint8_t> groupElemToBytes(const secp256k1_pubkey& pubkey);
secp256k1_pubkey groupElemFromBytes(const std::vector<uint8_t>& bytes);

// Function to generate random bytes using C++ standard library
void generateRandomBytes(unsigned char* buffer, size_t size);
// Function to generate random secret key
boost::multiprecision::cpp_int randomSecret();

#endif // MINING_H

