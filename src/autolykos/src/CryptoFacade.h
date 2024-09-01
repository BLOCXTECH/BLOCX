#ifndef CRYPTO_FACADE_H
#define CRYPTO_FACADE_H

#include "includes.h"

class CryptoFacade {
public:
     // Function to create a secp256k1 context
     static secp256k1_context* create_context();
     
     // Function to check if a public key is valid and not at infinity
     static bool isValidAndNotInfinity(const secp256k1_context* ctx, const secp256k1_pubkey& pubkey);
     
     // Function to convert boost::multiprecision::cpp_int to unsigned char array
     static void cpp_int_to_bytes(const boost::multiprecision::cpp_int& num, unsigned char* bytes, size_t size);
     
     // Function to compare two secp256k1_pubkey objects
     static bool compare_pubkeys(const secp256k1_context* ctx, const secp256k1_pubkey& pk1, const secp256k1_pubkey& pk2);
};

#endif // CRYPTO_FACADE_H
  
