#ifndef AUTOLYKOS_POW_SCHEME_H
#define AUTOLYKOS_POW_SCHEME_H

#include "includes.h"
/**
  * Autolykos PoW puzzle scheme reference implementation.
  *
  * Based on k-sum problem, so general idea is to find k numbers in a table of size N, such that
  * sum of numbers (or a hash of the sum) is less than target value.
  *
  * See https://docs.ergoplatform.com/ErgoPow.pdf for details
  *
  * CPU Mining process is implemented in inefficient way and should not be used in real environment.
  *
  * @see papers/yellow/pow/ErgoPow.tex for full description
  * @param k - number of elements in one solution
  * @param n - initially, N = 2^^n
  */
class AutolykosPowScheme {
public:
    AutolykosPowScheme(int k, int n);
    int calcN(int version, int headerHeight) const;
    int calcN(const HeaderWithoutPow& header) const;
    arith_uint256 getB(uint32_t nBits);
    std::vector<int> genIndexes(const std::vector<uint8_t>& seed, int N);
    boost::multiprecision::cpp_int genElement(int version,
                                          const std::vector<uint8_t>& m,
                                          const std::vector<uint8_t>& pk,
                                          const std::vector<uint8_t>& w,
                                          const std::vector<uint8_t>& indexBytes,
                                          const std::vector<uint8_t>& heightBytes);

    std::optional<AutolykosSolution> checkNonces(int version,
                                                 const std::vector<uint8_t>& h,
                                                 const std::vector<uint8_t>& m,
                                                 const boost::multiprecision::cpp_int& sk,
                                                 const boost::multiprecision::cpp_int& x,
                                                 const boost::multiprecision::cpp_int& b,
                                                 int N,
                                                 long startNonce,
                                                 long endNonce);

    std::vector<uint8_t> msgByHeader(const HeaderWithoutPow& h);

    bool checkPoWForVersion1(const Header& header, const boost::multiprecision::cpp_int& b);
    boost::multiprecision::cpp_int hitForVersion2(const Header& header);
    bool validate(const Header& header);
    
    std::tuple<ModifierId, int> derivedHeaderFields(const std::optional<Header>& parentOpt);

    std::optional<Header> prove(const std::optional<Header>& parentOpt,
                                ModifierId parentId,
                                int height,
                                Version version,
                                uint32_t nBits,
                                const ADDigest stateRoot,
                                const Digest32 adProofsRoot,
                                const Digest32 transactionsRoot,
                                Timestamp timestamp,
                                const Digest32 extensionHash,
                                const std::array<uint8_t, 3>& votes,
                                const boost::multiprecision::cpp_int& sk,
                                long minNonce = LLONG_MIN,
                                long maxNonce = LLONG_MAX);

    static int k;
    static int n;
    int NBase;

    //The toHeaderWithoutPow is not used in scala code it is added here because in c we can't able to pass the Header type to different type HeaderWithoutPow
    HeaderWithoutPow toHeaderWithoutPow(const Header& header) const {
        return HeaderWithoutPow(header.version, header.parentId, header.ADProofsRoot, header.stateRoot, header.transactionsRoot, header.timestamp, header.nBits, header.height, header.extensionRoot, header.votes);
    }

        
    // Function to convert a hex string to a vector of bytes
    std::vector<uint8_t> hexToBytesModifierId(const std::string& hex)
    {
        std::vector<uint8_t> bytes;

        // Ensure the string starts with "0x"
        std::string hex_str = (hex.substr(0, 2) == "0x") ? hex.substr(2) : hex;

        // Ensure the hex string represents a uint256 value
        if (hex_str.length() != 64) {
            throw std::invalid_argument("Hex string length is not 64 characters (32 bytes) for uint256.");
        }

        // Convert each pair of hexadecimal digits to a byte
        for (size_t i = 0; i < hex_str.length(); i += 2) {
            std::string byteString = hex_str.substr(i, 2);
            uint8_t byte = static_cast<uint8_t>(strtol(byteString.c_str(), nullptr, 16));
            bytes.push_back(byte);
        }

        return bytes;
    }

    // Function to convert a hex string to a std::array<uint8_t, 32>
    std::array<uint8_t, 32> hexToArrayDigest32(const std::string& hex)
    {
        std::array<uint8_t, 32> bytes{};

        // Ensure the string starts with "0x"
        std::string hex_str = (hex.substr(0, 2) == "0x") ? hex.substr(2) : hex;

        // Ensure the hex string represents 32 bytes (64 characters)
        if (hex_str.length() != 64) {
            throw std::invalid_argument("Hex string length is not 64 characters (32 bytes).");
        }

        // Convert each pair of hexadecimal digits to a byte
        for (size_t i = 0; i < 64; i += 2) {
            std::string byteString = hex_str.substr(i, 2);
            uint8_t byte = static_cast<uint8_t>(strtol(byteString.c_str(), nullptr, 16));
            bytes[i / 2] = byte;
        }

        return bytes;
    }

    std::vector<uint8_t> uint64ToBytes(uint64_t number) {
        std::vector<uint8_t> result(8, 0); // Initialize a vector of size 8 with all zeros
        for (size_t i = 0; i < 8; ++i) {
            result[7 - i] = static_cast<uint8_t>((number >> (8 * i)) & 0xFF); // Place bytes from the end
        }
        return result;
    }

    unsigned int cpp_int_to_unsigned_int(const boost::multiprecision::cpp_int& value) {
        if (value < 0) {
            throw std::overflow_error("Cannot convert negative value to unsigned int");
        }
        
        if (value > std::numeric_limits<unsigned int>::max()) {
            throw std::overflow_error("Value too large for unsigned int");
        }
        
        return static_cast<unsigned int>(value.convert_to<unsigned long long>());
    }

    std::string vectorToHex(const std::vector<uint8_t>& vec) {
        std::ostringstream oss;
        for (auto byte : vec) {
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
        }
        return oss.str();
    }


private:
    int HeaderInitialVersion = 1;
    int IncreaseStart;
    int IncreasePeriodForN;
    int NIncreasementHeightMax;
};

#endif // AUTOLYKOS_POW_SCHEME_H

