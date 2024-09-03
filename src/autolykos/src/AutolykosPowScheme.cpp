// Copyright (c) 2017-2024 The ERGO developers
// Copyright (c) 2023-2024 The BLOCX developers

#include "includes.h"
#include "hash.h"

using namespace boost::multiprecision;

int AutolykosPowScheme::k;
int AutolykosPowScheme::n; 

AutolykosPowScheme::AutolykosPowScheme() {
    int k = 32;
    int n = 26;
    AutolykosPowScheme::k = k;
    AutolykosPowScheme::n = n;    

    if (AutolykosPowScheme::k > 32) throw std::invalid_argument("k > 32 is not allowed due to genIndexes function");
    if (AutolykosPowScheme::n >= 31) throw std::invalid_argument("n >= 31 is not allowed");

    NBase = static_cast<int>(std::pow(2, AutolykosPowScheme::n));
    IncreaseStart = 600 * 1024;
    IncreasePeriodForN = 50 * 1024;
    NIncreasementHeightMax = 4198400;
}

/**
  * Calculates table size (N value) for a given height (moment of time)
  *
  * @see papers/yellow/pow/ErgoPow.tex for full description and test vectors
  * @param version - protocol version
  * @param headerHeight - height of a header to mine
  * @return - N value
  */
int AutolykosPowScheme::calcN(int version, int headerHeight) const {
    int height = std::min(NIncreasementHeightMax, headerHeight);
    if (height < IncreaseStart) {
        return NBase;
    } else {
        int itersNumber = (height - IncreaseStart) / IncreasePeriodForN + 1;
        int result = NBase;
        for (int i = 0; i < itersNumber; ++i) {
            result = result / 100 * 105;
        }
        return result;
    }
}

int AutolykosPowScheme::calcN(const HeaderWithoutPow& header) const {
    return calcN(header.version, header.height);
}

/**
  * Header digest ("message" for default GPU miners) a miner is working on
  */
std::vector<uint8_t> AutolykosPowScheme::msgByHeader(const HeaderWithoutPow& h) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    auto data = HeaderSerializer::bytesWithoutPow(h);
    for (const auto& byte : data) {
        ss << std::setw(2) << static_cast<int>(byte);
    }
    auto computedMsg = computeBlake2bHash(ss.str());
    return computedMsg;
}

cpp_int arith_uint256_to_cpp_int(const arith_uint256& a256)
{
    std::string hex_str = a256.GetHex();
    cpp_int result("0x" + hex_str);
    
    return result;

}

/**
  * Get target `b` from encoded difficulty `nBits`
  */
arith_uint256 AutolykosPowScheme::getB(uint32_t nBits) {
    arith_uint256 hashTarget = arith_uint256().SetCompact(nBits);
    return hashTarget;
}

/**
  * Hash function that takes `m` and `nonceBytes` and returns a list of size `k` with numbers in
  * [0,`N`)
  */
std::vector<int> AutolykosPowScheme::genIndexes(const std::vector<uint8_t>& seed, int N) {
    std::vector<uint8_t> hash = Blake2b256(seed);
    std::vector<uint8_t> extendedHash(hash);
    extendedHash.insert(extendedHash.end(), hash.begin(), hash.begin() + 3);

    std::vector<int> indexes;
    for (int i = 0; i < AutolykosPowScheme::k; ++i) {
        uint32_t index = boost::endian::big_to_native(*reinterpret_cast<const uint32_t*>(&extendedHash[i]));
        indexes.push_back(index % N);
    }

    // Ensuring the size of the vector is k
    if (indexes.size() != AutolykosPowScheme::k) {
        throw std::runtime_error("Incorrect length of indexes");
    }

    return indexes;
}

//These will concatenate the input data  
std::vector<uint8_t> concat(const std::initializer_list<std::vector<uint8_t>>& lists) {
    std::vector<uint8_t> result;
    size_t total_size = std::accumulate(lists.begin(), lists.end(), 0, [](size_t sum, const std::vector<uint8_t>& v) {
        return sum + v.size();
    });
    result.reserve(total_size);
    for (const auto& v : lists) {
        result.insert(result.end(), v.begin(), v.end());
    }
    return result;
}

/**
  * Constant data to be added to hash function to increase its calculation time
  */
const std::vector<uint8_t> M = []() {
    std::vector<uint8_t> d;
    d.reserve(1024 * 8); // Reserve space for 1024 long values (8 bytes each)
    
    for (int64_t i = 0; i < 1024; ++i) {
        for (int j = 7; j >= 0; --j) {
            d.push_back(static_cast<uint8_t>((i >> (j * 8)) & 0xFF));
        }
    }
    
    return d;
}();

/**
  * Generate element of Autolykos equation.
  */
cpp_int AutolykosPowScheme::genElement(const std::vector<uint8_t>& indexBytes,
                                       const std::vector<uint8_t>& heightBytes)
{
        auto hashed = hash(concat({indexBytes, heightBytes, M}));
        hashed.erase(hashed.begin()); // drop the first byte

        return toBigInt(hashed);
}

std::vector<uint8_t> resizeAsBigEndian(std::vector<uint8_t> vec, size_t newSize) {
    if (vec.size() < newSize) {
        vec.insert(vec.begin(), newSize - vec.size(), 0);
    } else if (vec.size() > newSize) {
        vec.resize(newSize);
    }
    return vec;
}

/**
 * Get hit for Autolykos v2 header (to test it then against PoW target)
 *
 * @param header - header to check PoW for
 * @return PoW hit
 */
boost::multiprecision::cpp_int AutolykosPowScheme::hit(const Header& header)
{
    const uint8_t version = 2;

    // Convert Header to HeaderWithoutPow
    HeaderWithoutPow headerWithoutPow = toHeaderWithoutPow(header);
    std::vector<uint8_t> msg = msgByHeader(headerWithoutPow);
    std::vector<uint8_t> nonce = header.powSolution.n;

    std::string hexString = vectorToHex(msg);

    std::vector<uint8_t> h(4);
    h[0] = (header.height >> 24) & 0xFF;
    h[1] = (header.height >> 16) & 0xFF;
    h[2] = (header.height >> 8) & 0xFF;
    h[3] = header.height & 0xFF;

    int N = calcN(headerWithoutPow);

    std::vector<uint8_t> hashResult = hash(concat({msg, nonce}));
    std::vector<uint8_t> prei8_bytes(hashResult.end() - 8, hashResult.end());
    bmp::cpp_int prei8 = toBigInt(prei8_bytes);

    std::vector<uint8_t> i = asUnsignedByteArray(prei8 % N);
    i = resizeAsBigEndian(i, 4);
    std::vector<uint8_t> f = Blake2b256(concat({i, h, M}));
    f.erase(f.begin());                                  // f.erase(f.begin()) is the same as takeRight(31)
    std::vector<uint8_t> seed = concat({f, msg, nonce});

    std::vector<int> indexes = genIndexes(seed, N);
    // pk and w not used in v2
    boost::multiprecision::cpp_int f2 = 0;
    for (int idx : indexes) {
        uint32_t index_big_endian = boost::endian::native_to_big(static_cast<uint32_t>(idx));
        std::vector<uint8_t> index_bytes(reinterpret_cast<uint8_t*>(&index_big_endian),
                                         reinterpret_cast<uint8_t*>(&index_big_endian) + 4);
        f2 += genElement(index_bytes, h); //{}=> NULL
    }

    // sum as byte array is always about 32 bytes
    std::vector<uint8_t> array = asUnsignedByteArray(f2);
    array = resizeAsBigEndian(array, 32);
    std::vector<uint8_t> ha = hash(array);
    return toBigInt(ha);
}

/**
 * Checks that `header` contains correct solution of the Autolykos PoW puzzle.
 */
bool AutolykosPowScheme::validate(const Header& header)
{
    boost::multiprecision::cpp_int b = arith_uint256_to_cpp_int(getB(header.nBits));
    boost::multiprecision::cpp_int h = hit(header);

    if (h >= b) {
        return false;
    }
    return true;
}