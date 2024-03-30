// Copyright (c) 2014-2023 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_COINJOIN_CLIENT_H
#define BITCOIN_COINJOIN_CLIENT_H

#include <coinjoin/util.h>
#include <coinjoin/coinjoin.h>
#include <util/translation.h>

#include <utility>
#include <atomic>

class CDeterministicMN;
using CDeterministicMNCPtr = std::shared_ptr<const CDeterministicMN>;

class CCoinJoinClientManager;
class CCoinJoinClientQueueManager;

class CConnman;
class CNode;
class CTxMemPool;

class UniValue;
class CMasternodeSync;


// The main object for accessing mixing
extern std::map<const std::string, std::shared_ptr<CCoinJoinClientManager>> coinJoinClientManagers;

// The object to track mixing queues
extern std::unique_ptr<CCoinJoinClientQueueManager> coinJoinClientQueueManager;

class CPendingDsaRequest
{
private:
    static constexpr int TIMEOUT = 15;

    CService addr;
    CCoinJoinAccept dsa;
    int64_t nTimeCreated{0};

public:
    CPendingDsaRequest() = default;

    CPendingDsaRequest(CService addr_, CCoinJoinAccept dsa_) :
        addr(std::move(addr_)),
        dsa(std::move(dsa_)),
        nTimeCreated(GetTime())
    {
    }

    [[nodiscard]] CService GetAddr() const { return addr; }
    [[nodiscard]] CCoinJoinAccept GetDSA() const { return dsa; }
    [[nodiscard]] bool IsExpired() const { return GetTime() - nTimeCreated > TIMEOUT; }

    friend bool operator==(const CPendingDsaRequest& a, const CPendingDsaRequest& b)
    {
        return a.addr == b.addr && a.dsa == b.dsa;
    }
    friend bool operator!=(const CPendingDsaRequest& a, const CPendingDsaRequest& b)
    {
        return !(a == b);
    }
    explicit operator bool() const
    {
        return *this != CPendingDsaRequest();
    }
};

class CCoinJoinClientSession : public CCoinJoinBaseSession
{
private:
    const std::unique_ptr<CMasternodeSync>& m_mn_sync;

    std::vector<COutPoint> vecOutPointLocked;

    bilingual_str strLastMessage;
    bilingual_str strAutoDenomResult;

    CDeterministicMNCPtr mixingMasternode;
    CMutableTransaction txMyCollateral; // client side collateral
    CPendingDsaRequest pendingDsaRequest;

    CKeyHolderStorage keyHolderStorage; // storage for keys used in PrepareDenominate

    CWallet& mixingWallet;

    /// Create denominations
    bool CreateDenominated(CAmount nBalanceToDenominate);
    bool CreateDenominated(CAmount nBalanceToDenominate, const CompactTallyItem& tallyItem, bool fCreateMixingCollaterals);

    /// Split up large inputs or make fee sized inputs
    bool MakeCollateralAmounts();
    bool MakeCollateralAmounts(const CompactTallyItem& tallyItem, bool fTryDenominated);

    bool CreateCollateralTransaction(CMutableTransaction& txCollateral, std::string& strReason);

    bool JoinExistingQueue(CAmount nBalanceNeedsAnonymized, CConnman& connman);
    bool StartNewQueue(CAmount nBalanceNeedsAnonymized, CConnman& connman);

    /// step 0: select denominated inputs and txouts
    bool SelectDenominate(std::string& strErrorRet, std::vector<CTxDSIn>& vecTxDSInRet);
    /// step 1: prepare denominated inputs and outputs
    bool PrepareDenominate(int nMinRounds, int nMaxRounds, std::string& strErrorRet, const std::vector<CTxDSIn>& vecTxDSIn, std::vector<std::pair<CTxDSIn, CTxOut> >& vecPSInOutPairsRet, bool fDryRun = false);
    /// step 2: send denominated inputs and outputs prepared in step 1
    bool SendDenominate(const std::vector<std::pair<CTxDSIn, CTxOut> >& vecPSInOutPairsIn, CConnman& connman) LOCKS_EXCLUDED(cs_coinjoin);

    /// Process Masternode updates about the progress of mixing
    void ProcessPoolStateUpdate(CCoinJoinStatusUpdate psssup);
    // Set the 'state' value, with some logging and capturing when the state changed
    void SetState(PoolState nStateNew);

    void CompletedTransaction(PoolMessage nMessageID);

    /// As a client, check and sign the final transaction
    bool SignFinalTransaction(const CTxMemPool& mempool, const CTransaction& finalTransactionNew, CNode& peer, CConnman& connman) LOCKS_EXCLUDED(cs_coinjoin);

    void RelayIn(const CCoinJoinEntry& entry, CConnman& connman) const;

    void SetNull() EXCLUSIVE_LOCKS_REQUIRED(cs_coinjoin);

public:
    explicit CCoinJoinClientSession(CWallet& pwallet, const std::unique_ptr<CMasternodeSync>& mn_sync) :
        m_mn_sync(mn_sync), mixingWallet(pwallet)
    {
    }

    void ProcessMessage(CNode& peer, CConnman& connman, const CTxMemPool& mempool, std::string_view msg_type, CDataStream& vRecv);

    void UnlockCoins();

    void ResetPool() LOCKS_EXCLUDED(cs_coinjoin);

    bilingual_str GetStatus(bool fWaitForBlock) const;

    bool GetMixingMasternodeInfo(CDeterministicMNCPtr& ret) const;

    /// Passively run mixing in the background according to the configuration in settings
    bool DoAutomaticDenominating(CTxMemPool& mempool, CConnman& connman, bool fDryRun = false) LOCKS_EXCLUDED(cs_coinjoin);

    /// As a client, submit part of a future mixing transaction to a Masternode to start the process
    bool SubmitDenominate(CConnman& connman);

    bool ProcessPendingDsaRequest(CConnman& connman);

    bool CheckTimeout();

    void GetJsonInfo(UniValue& obj) const;
};

/** Used to keep track of mixing queues
 */
class CCoinJoinClientQueueManager : public CCoinJoinBaseManager
{
private:
    CConnman& connman;
    const std::unique_ptr<CMasternodeSync>& m_mn_sync;

public:
    explicit CCoinJoinClientQueueManager(CConnman& _connman, const std::unique_ptr<CMasternodeSync>& mn_sync) :
        connman(_connman), m_mn_sync(mn_sync) {};

    void ProcessMessage(const CNode& peer, std::string_view msg_type, CDataStream& vRecv) LOCKS_EXCLUDED(cs_vecqueue);
    void ProcessDSQueue(const CNode& peer, CDataStream& vRecv);
    void DoMaintenance();
};

/** Used to keep track of current status of mixing pool
 */
class CCoinJoinClientManager
{
private:
    // Keep track of the used Masternodes
    std::vector<COutPoint> vecMasternodesUsed;

    const std::unique_ptr<CMasternodeSync>& m_mn_sync;

    mutable Mutex cs_deqsessions;
    // TODO: or map<denom, CCoinJoinClientSession> ??
    std::deque<CCoinJoinClientSession> deqSessions GUARDED_BY(cs_deqsessions);

    std::atomic<bool> fMixing{false};

    int nCachedLastSuccessBlock{0};
    int nMinBlocksToWait{1}; // how many blocks to wait for after one successful mixing tx in non-multisession mode
    bilingual_str strAutoDenomResult;

    CWallet& mixingWallet;

    // Keep track of current block height
    int nCachedBlockHeight{0};

    bool WaitForAnotherBlock() const;

    // Make sure we have enough keys since last backup
    bool CheckAutomaticBackup();

public:
    int nCachedNumBlocks{std::numeric_limits<int>::max()};    // used for the overview screen
    bool fCreateAutoBackups{true}; // builtin support for automatic backups

    CCoinJoinClientManager() = delete;
    CCoinJoinClientManager(CCoinJoinClientManager const&) = delete;
    CCoinJoinClientManager& operator=(CCoinJoinClientManager const&) = delete;

    explicit CCoinJoinClientManager(CWallet& wallet, const std::unique_ptr<CMasternodeSync>& mn_sync) :
        m_mn_sync(mn_sync), mixingWallet(wallet) {}

    void ProcessMessage(CNode& peer, CConnman& connman, const CTxMemPool& mempool, std::string_view msg_type, CDataStream& vRecv) LOCKS_EXCLUDED(cs_deqsessions);

    bool StartMixing();
    void StopMixing();
    bool IsMixing() const;
    void ResetPool() LOCKS_EXCLUDED(cs_deqsessions);

    bilingual_str GetStatuses() LOCKS_EXCLUDED(cs_deqsessions);
    std::string GetSessionDenoms() LOCKS_EXCLUDED(cs_deqsessions);

    bool GetMixingMasternodesInfo(std::vector<CDeterministicMNCPtr>& vecDmnsRet) const LOCKS_EXCLUDED(cs_deqsessions);

    /// Passively run mixing in the background according to the configuration in settings
    bool DoAutomaticDenominating(CTxMemPool& mempool, CConnman& connman, bool fDryRun = false) LOCKS_EXCLUDED(cs_deqsessions);

    bool TrySubmitDenominate(const CService& mnAddr, CConnman& connman) LOCKS_EXCLUDED(cs_deqsessions);
    bool MarkAlreadyJoinedQueueAsTried(CCoinJoinQueue& dsq) const LOCKS_EXCLUDED(cs_deqsessions);

    void CheckTimeout() LOCKS_EXCLUDED(cs_deqsessions);

    void ProcessPendingDsaRequest(CConnman& connman) LOCKS_EXCLUDED(cs_deqsessions);

    void AddUsedMasternode(const COutPoint& outpointMn);
    CDeterministicMNCPtr GetRandomNotUsedMasternode();

    void UpdatedSuccessBlock();

    void UpdatedBlockTip(const CBlockIndex* pindex);

    void DoMaintenance(CTxMemPool& mempool, CConnman& connman);

    void GetJsonInfo(UniValue& obj) const LOCKS_EXCLUDED(cs_deqsessions);
};


void DoCoinJoinMaintenance(CTxMemPool& mempool, CConnman& connman);

#endif // BITCOIN_COINJOIN_CLIENT_H
