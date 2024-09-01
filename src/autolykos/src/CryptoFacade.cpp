#include "includes.h"

// Function to create a secp256k1 context
secp256k1_context* CryptoFacade::create_context() {
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    if (ctx == nullptr) {
        throw std::runtime_error("Failed to create secp256k1 context");
    }
    return ctx;
}

// Function to check if a public key is valid and not at infinity
bool CryptoFacade::isValidAndNotInfinity(const secp256k1_context* ctx, const secp256k1_pubkey& pubkey) {
    unsigned char serialized[65];
    size_t serialized_length = 65;

    if (!secp256k1_ec_pubkey_serialize(ctx, serialized, &serialized_length, &pubkey, SECP256K1_EC_UNCOMPRESSED)) {
        return false;
    }

    // Check if the serialized public key is all zeros (which means it's at infinity)
    for (size_t i = 0; i < serialized_length; ++i) {
        if (serialized[i] != 0) {
            return true;
        }
    }
    return false;
}

// Function to convert boost::multiprecision::cpp_int to unsigned char array
void CryptoFacade::cpp_int_to_bytes(const boost::multiprecision::cpp_int& num, unsigned char* bytes, size_t size) {
    boost::multiprecision::cpp_int temp = num;
    for (size_t i = 0; i < size; ++i) {
        bytes[size - 1 - i] = static_cast<unsigned char>(temp & 0xFF);
        temp >>= 8;
    }
}

// Function to compare two secp256k1_pubkey objects
bool CryptoFacade::compare_pubkeys(const secp256k1_context* ctx, const secp256k1_pubkey& pk1, const secp256k1_pubkey& pk2) {
    unsigned char output1[65];
    unsigned char output2[65];
    size_t output_length1 = 65;
    size_t output_length2 = 65;

    secp256k1_ec_pubkey_serialize(ctx, output1, &output_length1, &pk1, SECP256K1_EC_UNCOMPRESSED);
    secp256k1_ec_pubkey_serialize(ctx, output2, &output_length2, &pk2, SECP256K1_EC_UNCOMPRESSED);

    return std::memcmp(output1, output2, std::min(output_length1, output_length2)) == 0;
}
