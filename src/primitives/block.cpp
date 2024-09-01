// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/block.h>

#include <autolykos/src/AutolykosPowScheme.h>
#include <autolykos/src/HeaderWithoutPow.h>
#include <autolykos/src/mining.h>
#include <hash.h>
#include <streams.h>
#include <tinyformat.h>
#include <vector>

uint256 CBlockHeader::GetHash() const
{
    std::vector<unsigned char> vch(80);
    CVectorWriter ss(SER_GETHASH, PROTOCOL_VERSION, vch, 0);
    ss << *this;
    // currently just returning HashX11 here
    if (nVersion != 0x05) {
        return HashX11((const char*)vch.data(), (const char*)vch.data() + vch.size());
    } else {
        return AutolykosHash();
    }
}

std::string vectorsToHex(const std::vector<uint8_t>& data) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (const auto& byte : data) {
        ss << std::setw(2) << static_cast<int>(byte);
    }
    return ss.str();
}

uint256 CBlockHeader::AutolykosHash() const
{
    Version version = 0x05;


    int k = 32;
    int n = 26;
    AutolykosPowScheme powScheme(k, n);

    std::string prevHash = hashPrevBlock.ToString();
    std::string merkleRoot = hashMerkleRoot.ToString();

    Timestamp timestamp = nTime;
    uint32_t nBitss = nBits;
    int a_Height = aHeight;

    std::vector<uint8_t> nonce = powScheme.uint64ToBytes(nNewNonce);
    ModifierId parentId = powScheme.hexToBytesModifierId(prevHash);
    Digest32 transactionsRoot = powScheme.hexToArrayDigest32(merkleRoot);

    Digest32 ADProofsRoot = {};
    ADDigest stateRoot = {};
    Digest32 extensionRoot = {};

    AutolykosSolution powSolution = {
        groupElemFromBytes({0x02, 0xf5, 0x92, 0x4b, 0x14, 0x32, 0x5a, 0x1f, 0xfa, 0x8f, 0x95, 0xf8, 0xc0, 0x00, 0x06, 0x11, 0x87, 0x28, 0xce, 0x37, 0x85, 0xa6, 0x48, 0xe8, 0xb2, 0x69, 0x82, 0x0a, 0x3d, 0x3b, 0xdf, 0xd4, 0x0d}),
        groupElemFromBytes({0x02, 0xf5, 0x92, 0x4b, 0x14, 0x32, 0x5a, 0x1f, 0xfa, 0x8f, 0x95, 0xf8, 0xc0, 0x00, 0x06, 0x11, 0x87, 0x28, 0xce, 0x37, 0x85, 0xa6, 0x48, 0xe8, 0xb2, 0x69, 0x82, 0x0a, 0x3d, 0x3b, 0xdf, 0xd4, 0x0d}),
        nonce,
        boost::multiprecision::cpp_int("0")};

    std::array<uint8_t, 3> votes = {0x00, 0x00, 0x00};

    Header h(
        version,
        parentId,
        ADProofsRoot,
        stateRoot,
        transactionsRoot,
        timestamp,
        nBitss,
        a_Height,
        extensionRoot,
        powSolution,
        votes);



    std::stringstream ss;
    ss << std::hex << powScheme.hitForVersion2(h);
    std::string hexStr = ss.str();
    return uint256S(hexStr);
}

std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, ver=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, nNewNonce=%d, aHeight=%d, vtx=%u)\n",
        GetHash().ToString(),
        nVersion,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        nTime, nBits, nNonce, nNewNonce, aHeight,
        vtx.size());
    for (const auto& tx : vtx) {
        s << "  " << tx->ToString() << "\n";
    }
    return s.str();
}

static void MarkVersionAsMostRecent(std::list<int32_t>& last_unique_versions, std::list<int32_t>::const_iterator version_it)
{
    if (version_it != last_unique_versions.cbegin()) {
        // Move the found version to the front of the list
        last_unique_versions.splice(last_unique_versions.begin(), last_unique_versions, version_it, std::next(version_it));
    }
}

static void SaveVersionAsMostRecent(std::list<int32_t>& last_unique_versions, const int32_t version)
{
    last_unique_versions.push_front(version);

    // Always keep the last 7 unique versions
    constexpr std::size_t max_backwards_look_ups = 7;
    if (last_unique_versions.size() > max_backwards_look_ups) {
        // Evict the oldest version
        last_unique_versions.pop_back();
    }
}

void CompressibleBlockHeader::Compress(const std::vector<CompressibleBlockHeader>& previous_blocks, std::list<int32_t>& last_unique_versions)
{
    if (previous_blocks.empty()) {
        // Previous block not available, we have to send the block completely uncompressed
        SaveVersionAsMostRecent(last_unique_versions, nVersion);
        return;
    }

    // Try to compress version
    const auto version_it = std::find(last_unique_versions.cbegin(), last_unique_versions.cend(), nVersion);
    if (version_it != last_unique_versions.cend()) {
        // Version is found in the last 7 unique blocks.
        bit_field.SetVersionOffset(static_cast<uint8_t>(std::distance(last_unique_versions.cbegin(), version_it) + 1));

        // Mark the version as the most recent one
        MarkVersionAsMostRecent(last_unique_versions, version_it);
    } else {
        // Save the version as the most recent one
        SaveVersionAsMostRecent(last_unique_versions, nVersion);
    }

    // Previous block is available
    const auto& last_block = previous_blocks.back();
    bit_field.MarkAsCompressed(CompressedHeaderBitField::Flag::PREV_BLOCK_HASH);

    // Compute compressed time diff
    const int64_t time_diff = nTime - last_block.nTime;
    if (time_diff <= std::numeric_limits<int16_t>::max() && time_diff >= std::numeric_limits<int16_t>::min()) {
        time_offset = static_cast<int16_t>(time_diff);
        bit_field.MarkAsCompressed(CompressedHeaderBitField::Flag::TIMESTAMP);
    }

    // If n_bits matches previous block, it can be compressed (not sent at all)
    if (nBits == last_block.nBits) {
        bit_field.MarkAsCompressed(CompressedHeaderBitField::Flag::NBITS);
    }
}

void CompressibleBlockHeader::Uncompress(const std::vector<CBlockHeader>& previous_blocks, std::list<int32_t>& last_unique_versions)
{
    if (previous_blocks.empty()) {
        // First block in chain is always uncompressed
        SaveVersionAsMostRecent(last_unique_versions, nVersion);
        return;
    }

    // We have the previous block
    const auto& last_block = previous_blocks.back();

    // Uncompress version
    if (bit_field.IsVersionCompressed()) {
        const auto version_offset = bit_field.GetVersionOffset();
        if (version_offset <= last_unique_versions.size()) {
            auto version_it = last_unique_versions.begin();
            std::advance(version_it, version_offset - 1);
            nVersion = *version_it;
            MarkVersionAsMostRecent(last_unique_versions, version_it);
        }
    } else {
        // Save the version as the most recent one
        SaveVersionAsMostRecent(last_unique_versions, nVersion);
    }

    // Uncompress prev block hash
    if (bit_field.IsCompressed(CompressedHeaderBitField::Flag::PREV_BLOCK_HASH)) {
        hashPrevBlock = last_block.GetHash();
    }

    // Uncompress timestamp
    if (bit_field.IsCompressed(CompressedHeaderBitField::Flag::TIMESTAMP)) {
        nTime = last_block.nTime + time_offset;
    }

    // Uncompress n_bits
    if (bit_field.IsCompressed(CompressedHeaderBitField::Flag::NBITS)) {
        nBits = last_block.nBits;
    }
}
