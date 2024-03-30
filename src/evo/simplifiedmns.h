// Copyright (c) 2017-2022 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_EVO_SIMPLIFIEDMNS_H
#define BITCOIN_EVO_SIMPLIFIEDMNS_H

#include <bls/bls.h>
#include <evo/deterministicmns.h>
#include <evo/dmn_types.h>
#include <merkleblock.h>
#include <netaddress.h>
#include <pubkey.h>

class UniValue;
class CBlockIndex;
class CDeterministicMNList;
class CDeterministicMN;

namespace llmq {
class CFinalCommitment;
class CQuorumBlockProcessor;
} // namespace llmq

class CSimplifiedMNListEntry
{
public:
    static constexpr uint16_t LEGACY_BLS_VERSION = 1;
    static constexpr uint16_t BASIC_BLS_VERSION = 2;

    uint256 proRegTxHash;
    uint256 confirmedHash;
    CService service;
    CBLSLazyPublicKey pubKeyOperator;
    CKeyID keyIDVoting;
    bool isValid{false};
    MnType nType{MnType::Standard_Masternode};
    uint16_t platformHTTPPort{0};
    uint160 platformNodeID{};
    CScript scriptPayout; // mem-only
    CScript scriptOperatorPayout; // mem-only
    uint16_t nVersion{LEGACY_BLS_VERSION};

    CSimplifiedMNListEntry() = default;
    explicit CSimplifiedMNListEntry(const CDeterministicMN& dmn);

    bool operator==(const CSimplifiedMNListEntry& rhs) const
    {
        return proRegTxHash == rhs.proRegTxHash &&
               confirmedHash == rhs.confirmedHash &&
               service == rhs.service &&
               pubKeyOperator == rhs.pubKeyOperator &&
               keyIDVoting == rhs.keyIDVoting &&
               isValid == rhs.isValid &&
               nVersion == rhs.nVersion &&
               nType == rhs.nType &&
               platformHTTPPort == rhs.platformHTTPPort &&
               platformNodeID == rhs.platformNodeID;
    }

    bool operator!=(const CSimplifiedMNListEntry& rhs) const
    {
        return !(rhs == *this);
    }

    SERIALIZE_METHODS(CSimplifiedMNListEntry, obj)
    {
        if ((s.GetType() & SER_NETWORK) && s.GetVersion() >= SMNLE_VERSIONED_PROTO_VERSION) {
            READWRITE(obj.nVersion);
        }
        READWRITE(
                obj.proRegTxHash,
                obj.confirmedHash,
                obj.service,
                CBLSLazyPublicKeyVersionWrapper(const_cast<CBLSLazyPublicKey&>(obj.pubKeyOperator), (obj.nVersion == LEGACY_BLS_VERSION)),
                obj.keyIDVoting,
                obj.isValid
                );
        if ((s.GetType() & SER_NETWORK) && s.GetVersion() < DMN_TYPE_PROTO_VERSION) {
            return;
        }
        if (obj.nVersion == BASIC_BLS_VERSION) {
            READWRITE(obj.nType);
            // if (obj.nType == MnType::Lite) {
            //     READWRITE(obj.platformHTTPPort);
            //     READWRITE(obj.platformNodeID);
            // }
        }
    }

    uint256 CalcHash() const;

    std::string ToString() const;
    void ToJson(UniValue& obj, bool extended = false) const;
};

class CSimplifiedMNList
{
public:
    std::vector<std::unique_ptr<CSimplifiedMNListEntry>> mnList;

    CSimplifiedMNList() = default;
    explicit CSimplifiedMNList(const std::vector<CSimplifiedMNListEntry>& smlEntries);
    explicit CSimplifiedMNList(const CDeterministicMNList& dmnList);

    uint256 CalcMerkleRoot(bool* pmutated = nullptr) const;
    bool operator==(const CSimplifiedMNList& rhs) const;
};

/// P2P messages

class CGetSimplifiedMNListDiff
{
public:
    uint256 baseBlockHash;
    uint256 blockHash;

    SERIALIZE_METHODS(CGetSimplifiedMNListDiff, obj)
    {
        READWRITE(obj.baseBlockHash, obj.blockHash);
    }
};

class CSimplifiedMNListDiff
{
public:
    static constexpr uint16_t CURRENT_VERSION = 1;

    uint256 baseBlockHash;
    uint256 blockHash;
    CPartialMerkleTree cbTxMerkleTree;
    CTransactionRef cbTx;
    std::vector<uint256> deletedMNs;
    std::vector<CSimplifiedMNListEntry> mnList;
    uint16_t nVersion{CURRENT_VERSION};

    std::vector<std::pair<uint8_t, uint256>> deletedQuorums; // p<LLMQType, quorumHash>
    std::vector<llmq::CFinalCommitment> newQuorums;

    SERIALIZE_METHODS(CSimplifiedMNListDiff, obj)
    {
        READWRITE(obj.baseBlockHash, obj.blockHash, obj.cbTxMerkleTree, obj.cbTx);
        if ((s.GetType() & SER_NETWORK) && s.GetVersion() >= BLS_SCHEME_PROTO_VERSION) {
            READWRITE(obj.nVersion);
        }
        READWRITE(obj.deletedMNs, obj.mnList);
        READWRITE(obj.deletedQuorums, obj.newQuorums);
    }

    CSimplifiedMNListDiff();
    ~CSimplifiedMNListDiff();

    bool BuildQuorumsDiff(const CBlockIndex* baseBlockIndex, const CBlockIndex* blockIndex,
                          const llmq::CQuorumBlockProcessor& quorum_block_processor);

    void ToJson(UniValue& obj, bool extended = false) const;
};

bool BuildSimplifiedMNListDiff(const uint256& baseBlockHash, const uint256& blockHash, CSimplifiedMNListDiff& mnListDiffRet,
                               const llmq::CQuorumBlockProcessor& quorum_block_processor, std::string& errorRet, bool extended = false);

#endif // BITCOIN_EVO_SIMPLIFIEDMNS_H
