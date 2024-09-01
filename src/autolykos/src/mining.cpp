#include "includes.h"

namespace bmp = boost::multiprecision;

// Group order, used in Autolykos V.1 for non-outsourceability,
// and also to obtain target in both Autolykos v1 and v2
const bmp::cpp_int q("115792089237316195423570985008687907852837564279074904382605163141518161494337");

// Instantiate NumericHash with q
NumericHash hashFn(q);

/**
 * Hash function which output is in Zq. Used in Autolykos v.1
 * @param in - input (bit-string)
 * @return - output (in Zq)
 */
bmp::cpp_int hashModQ(const std::vector<uint8_t>& in) {
    return hashFn.hash(in);
}

/**
  * Convert byte array to unsigned integer
  * @param in - byte array
  * @return - unsigned integer
  */
bmp::cpp_int toBigInt(const std::vector<uint8_t>& data) {
    bmp::cpp_int result;
    import_bits(result, data.begin(), data.end());
    return result;
}

/**
 * Blake2b256 hash function invocation
 * @param in - input bit-string
 * @return - 256 bits (32 bytes) array
 */
std::vector<uint8_t> hash(const std::vector<uint8_t>& in) {
    return Blake2b256(in);
}

// Function to generate a public key from a private key
secp256k1_pubkey genPk(const bmp::cpp_int& privateKey) {
    std::vector<int8_t> privateKeyBytes(32); // Initialize a 32-byte array
    export_bits(privateKey, privateKeyBytes.rbegin(), 8, false); // Convert cpp_int to byte array

    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    secp256k1_pubkey pubkey;

    // Create the public key from the private key
    if (secp256k1_ec_pubkey_create(ctx, &pubkey, reinterpret_cast<const uint8_t*>(privateKeyBytes.data())) != 1) {
        secp256k1_context_destroy(ctx);
        throw std::runtime_error("Failed to create public key");
    }

    secp256k1_context_destroy(ctx);
    return pubkey;
}

std::vector<uint8_t> groupElemToBytes(const secp256k1_pubkey& pubkey) {
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);

    // Serialize the public key
    std::vector<uint8_t> serializedPubkey(33); // 33 bytes for a compressed public key
    size_t outputLength = serializedPubkey.size();
    secp256k1_ec_pubkey_serialize(ctx, serializedPubkey.data(), &outputLength, &pubkey, SECP256K1_EC_COMPRESSED);

    secp256k1_context_destroy(ctx);
    return serializedPubkey;
}

secp256k1_pubkey groupElemFromBytes(const std::vector<uint8_t>& bytes) {
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    secp256k1_pubkey pubkey;

    // Parse the public key
    int result = secp256k1_ec_pubkey_parse(ctx, &pubkey, bytes.data(), bytes.size());

    if (result != 1) {
        std::cout << "Error: secp256k1_ec_pubkey_parse failed: " << result << std::endl;
    }

    secp256k1_context_destroy(ctx);
    return pubkey;
}


// Function to generate random bytes using C++ standard library
void generateRandomBytes(unsigned char* buffer, size_t size) {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned char> dis(0, 255);

    for (size_t i = 0; i < size; ++i) {
        buffer[i] = dis(gen);
    }
}

// Function to generate random secret key
boost::multiprecision::cpp_int randomSecret() {
    std::vector<unsigned char> secret(32); // secp256k1 private key size is 32 bytes
    int ret;

    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    // Generate random secret key
    do {
        generateRandomBytes(secret.data(), secret.size());
        ret = secp256k1_ec_seckey_verify(ctx, secret.data());
    } while (!ret);

    boost::multiprecision::cpp_int secretInt;
    for (size_t i = 0; i < secret.size(); ++i) {
        secretInt = (secretInt << 8) | secret[i];
    }

    secp256k1_context_destroy(ctx);
    return secretInt;
}

