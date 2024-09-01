#include "includes.h"
#include "hash.h"

using namespace boost::multiprecision;

int AutolykosPowScheme::k;
int AutolykosPowScheme::n; 

AutolykosPowScheme::AutolykosPowScheme(int k, int n) {
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
    if (version == HeaderInitialVersion) {
        return NBase;
    } else {
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
}

int AutolykosPowScheme::calcN(const HeaderWithoutPow& header) const {
    return calcN(header.version, header.height);
}

std::string toHex(const std::vector<uint8_t>& data) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');  // Set the format for hexadecimal output

    for (auto byte : data) {
        ss << std::setw(2) << static_cast<int>(byte);  // Format each byte as two hex digits
    }

    return ss.str();
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
cpp_int AutolykosPowScheme::genElement(int version,
                                       const std::vector<uint8_t>& m,
                                       const std::vector<uint8_t>& pk,
                                       const std::vector<uint8_t>& w,
                                       const std::vector<uint8_t>& indexBytes,
                                       const std::vector<uint8_t>& heightBytes)
{
        auto hashed = hash(concat({indexBytes, heightBytes, M}));
        hashed.erase(hashed.begin()); // drop the first byte

        return toBigInt(hashed);
}

/**
 * Check nonces from `startNonce` to `endNonce` for message `m`, secrets `sk` and `x`, difficulty `b`.
 * Return AutolykosSolution if there is any valid nonce in this interval.
 */
std::optional<AutolykosSolution> AutolykosPowScheme::checkNonces(int version,
                                                                 const std::vector<uint8_t>& h,
                                                                 const std::vector<uint8_t>& m,
                                                                 const cpp_int& sk,
                                                                 const cpp_int& x,
                                                                 const cpp_int& b,
                                                                 int N,
                                                                 long startNonce,
                                                                 long endNonce)
{
    std::cout << "Going to check nonces from " << startNonce << " to " << endNonce << std::endl;

    std::vector<uint8_t> p1 = groupElemToBytes(genPk(sk));
    std::vector<uint8_t> p2 = groupElemToBytes(genPk(x));

    auto loop = [&](long i) -> std::optional<AutolykosSolution> {
        while (i != endNonce) {
            if (i % 1000000 == 0 && i > 0) {
                std::cout << i << "nonce tested" << std::endl;
            }

            std::vector<uint8_t> nonce(8);
            for (int j = 0; j < 8; ++j) {
                nonce[7 - j] = static_cast<uint8_t>(i >> (j * 8));
            }

            std::vector<uint8_t> seed;
                std::vector<uint8_t> temp = hash(concat({m, nonce}));
                std::vector<uint8_t> tempEnd(temp.end() - 8, temp.end());
                bmp::cpp_int i_bigint = toBigInt(tempEnd);
                std::vector<uint8_t> i_bytes = asUnsignedByteArray(i_bigint % N);
                i_bytes.resize(4);
                std::vector<uint8_t> f = Blake2b256(concat({i_bytes, h, M}));
                f.erase(f.begin());
                seed = concat({f, m, nonce});

            bmp::cpp_int d;
                bmp::cpp_int sum = 0;
                for (int index : genIndexes(seed, N)) {
                    uint32_t index_big_endian = boost::endian::native_to_big(static_cast<uint32_t>(index));
                    std::vector<uint8_t> index_bytes(reinterpret_cast<uint8_t*>(&index_big_endian),
                                                     reinterpret_cast<uint8_t*>(&index_big_endian) + 4);
                    sum += genElement(version, m, p1, p2, index_bytes, h);
                }
                d = toBigInt(hash(asUnsignedByteArray(sum)));

            if (d <= b) {
                return AutolykosSolution{genPk(sk), genPk(x), nonce, d};
            }
            ++i;
        }
        return std::nullopt;
    };

    return loop(startNonce);
}

// Utility function to print secp256k1 pubkey
void printPubKey(const char* msg, const secp256k1_pubkey& pubkey, secp256k1_context* ctx)
{
    unsigned char output[65];
    size_t output_length = 65;
    secp256k1_ec_pubkey_serialize(ctx, output, &output_length, &pubkey, SECP256K1_EC_UNCOMPRESSED);
    std::cout << msg << ": ";
    for (size_t i = 0; i < output_length; ++i) {
        std::cout << std::hex << std::setfill('0') << std::setw(2) << (int)output[i];
    }
    std::cout << std::endl;
}

/**
 * Check PoW for Autolykos v1 header
 *
 * @param header - header to check PoW for
 * @param b - PoW target
 * @return whether PoW is valid or not
 */
bool AutolykosPowScheme::checkPoWForVersion1(const Header& header, const boost::multiprecision::cpp_int& b)
{
    uint8_t version = 1;

    // Convert Header to HeaderWithoutPow
    HeaderWithoutPow headerWithoutPow = toHeaderWithoutPow(header);
    std::vector<uint8_t> msg = msgByHeader(headerWithoutPow);
    const AutolykosSolution& s = header.powSolution;

    std::vector<uint8_t> nonce = s.n;

    int N = calcN(headerWithoutPow);

    if (s.d >= b) {
        throw std::runtime_error("Incorrect d for b");
    }

    // Context creation
    secp256k1_context* ctx = CryptoFacade::create_context();
    // Ensure pk and w are valid
    if (!CryptoFacade::isValidAndNotInfinity(ctx, s.pk)) {
        secp256k1_context_destroy(ctx);
        throw std::runtime_error("pk is incorrect");
    }
    if (!CryptoFacade::isValidAndNotInfinity(ctx, s.w)) {
        secp256k1_context_destroy(ctx);
        throw std::runtime_error("w is incorrect");
    }

    std::vector<uint8_t> pkBytes = groupElemToBytes(s.pk);
    std::vector<uint8_t> wBytes = groupElemToBytes(s.w);

    std::vector<uint8_t> seed = concat({msg, nonce}); // Autolykos v1, Alg. 2, line4: m || nonce
    std::vector<int> indexes = genIndexes(seed, N);

    // height is not used in v1
    boost::multiprecision::cpp_int f = 0;
    for (const auto& idx : indexes) {
        uint32_t index_big_endian = boost::endian::native_to_big(static_cast<uint32_t>(idx));
        std::vector<uint8_t> index_bytes(reinterpret_cast<uint8_t*>(&index_big_endian),
                                         reinterpret_cast<uint8_t*>(&index_big_endian) + 4);
        f += genElement(version, msg, pkBytes, wBytes, index_bytes, {});
    }
    f %= q;

    std::cout << "d: " << s.d << std::endl;
    std::cout << "f: " << f << std::endl;

    // Perform exponentiation of point w by scalar f
    unsigned char f_bytes[32];
    unsigned char d_bytes[32];
    CryptoFacade::cpp_int_to_bytes(f, f_bytes, 32);
    CryptoFacade::cpp_int_to_bytes(s.d, d_bytes, 32);
    auto pk = s.pk;
    auto w = s.w;

    secp256k1_pubkey left;
    if (!secp256k1_ec_pubkey_tweak_mul(ctx, &w, f_bytes)) {
        throw std::runtime_error("Failed to exponentiate w by f");
    }
    left = w;

    // Perform exponentiation of generator G by scalar d
    secp256k1_pubkey Gd;
    if (!secp256k1_ec_pubkey_create(ctx, &Gd, d_bytes)) {
        throw std::runtime_error("Failed to exponentiate G by d");
    }

    // Multiply points G^d and s.pk
    secp256k1_pubkey right;
    const secp256k1_pubkey* pubkeys[] = {&Gd, &pk};
    if (!secp256k1_ec_pubkey_combine(ctx, &right, pubkeys, 2)) {
        throw std::runtime_error("Failed to combine G^d and pk");
    }

    // Print results
    printPubKey("left", left, ctx);
    printPubKey("right", right, ctx);

    // Compare left and right
    bool are_equal = CryptoFacade::compare_pubkeys(ctx, left, right);
    std::cout << "left == right: " << (are_equal ? "true" : "false") << std::endl;

    // Clean up
    secp256k1_context_destroy(ctx);

    return are_equal;
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
boost::multiprecision::cpp_int AutolykosPowScheme::hitForVersion2(const Header& header)
{
    const uint8_t version = 2;

    // Convert Header to HeaderWithoutPow
    HeaderWithoutPow headerWithoutPow = toHeaderWithoutPow(header);
    std::vector<uint8_t> msg = msgByHeader(headerWithoutPow);
    std::vector<uint8_t> nonce = header.powSolution.n;

    std::string hexString = vectorToHex(msg);
    // std::cout << "hitForVersion2 msg " << hexString << std::endl;

    std::vector<uint8_t> h(4); // used in AL v.2 only
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
    std::vector<uint8_t> seed = concat({f, msg, nonce}); // Autolykos v1, Alg. 2, line4:

    std::vector<int> indexes = genIndexes(seed, N);
    // pk and w not used in v2
    boost::multiprecision::cpp_int f2 = 0;
    for (int idx : indexes) {
        uint32_t index_big_endian = boost::endian::native_to_big(static_cast<uint32_t>(idx));
        std::vector<uint8_t> index_bytes(reinterpret_cast<uint8_t*>(&index_big_endian),
                                         reinterpret_cast<uint8_t*>(&index_big_endian) + 4);
        f2 += genElement(version, msg, {}, {}, index_bytes, h); //{}=> NULL
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
    boost::multiprecision::cpp_int hit = hitForVersion2(header);

    if (hit >= b) {
        return false;
    }
    return true;
}

/**
 * Calculate header fields based on its parent, namely, header's parent id and height
 */
std::tuple<ModifierId, int> AutolykosPowScheme::derivedHeaderFields(const std::optional<Header>& parentOpt)
{
    ErgoNodeViewModifier HeaderToBytes;
    int height = parentOpt ? parentOpt->height + 1 : HeaderToBytes.GenesisHeight;
    ModifierId parentId = parentOpt ? HeaderToBytes.id(*parentOpt) : HeaderToBytes.GenesisParentId();
    return {parentId, height};
}

std::optional<Header> AutolykosPowScheme::prove(const std::optional<Header>& parentOpt,
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
                                                long minNonce,
                                                long maxNonce)
{

    HeaderWithoutPow h(version, parentId, adProofsRoot, stateRoot, transactionsRoot, timestamp,
                       nBits, height, extensionHash, votes);
    auto msg = msgByHeader(h);
    auto b = arith_uint256_to_cpp_int(getB(nBits));
    auto x = randomSecret();

    std::vector<uint8_t> hbs(4);
    hbs[0] = (h.height >> 24) & 0xFF;
    hbs[1] = (h.height >> 16) & 0xFF;
    hbs[2] = (h.height >> 8) & 0xFF;
    hbs[3] = h.height & 0xFF;
    auto N = calcN(h);

    auto solution = checkNonces(version, hbs, msg, sk, x, b, N, minNonce, maxNonce);
    if (solution) {
        return h.toHeader(*solution);
    } else {
        return std::nullopt;
    }
}
