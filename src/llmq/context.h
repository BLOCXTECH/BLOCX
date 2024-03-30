// Copyright (c) 2018-2022 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LLMQ_CONTEXT_H
#define BITCOIN_LLMQ_CONTEXT_H

#include <memory>

class CBLSWorker;
class CConnman;
class CDBWrapper;
class CEvoDB;
class CTxMemPool;
class CSporkManager;

namespace llmq {
class CDKGDebugManager;
class CQuorumBlockProcessor;
class CDKGSessionManager;
class CQuorumManager;
class CSigSharesManager;
class CSigningManager;
class CChainLocksHandler;
class CInstantSendManager;
}

struct LLMQContext {
    LLMQContext(CEvoDB& evoDb, CTxMemPool& mempool, CConnman& connman, CSporkManager& sporkManager, bool unitTests, bool fWipe);
    ~LLMQContext();

    void Create(CEvoDB& evoDb, CTxMemPool& mempool, CConnman& connman, CSporkManager& sporkManager, bool unitTests, bool fWipe);
    void Destroy();
    void Interrupt();
    void Start();
    void Stop();

    std::shared_ptr<CBLSWorker> bls_worker;
    std::unique_ptr<llmq::CDKGDebugManager> dkg_debugman;
    std::unique_ptr<llmq::CDKGSessionManager> qdkgsman;
    std::unique_ptr<llmq::CSigSharesManager> shareman;
    std::unique_ptr<llmq::CSigningManager> sigman;

    llmq::CQuorumBlockProcessor* quorum_block_processor{nullptr};
    llmq::CQuorumManager* qman{nullptr};
    llmq::CChainLocksHandler* clhandler{nullptr};
    llmq::CInstantSendManager* isman{nullptr};
};

#endif // BITCOIN_LLMQ_CONTEXT_H
