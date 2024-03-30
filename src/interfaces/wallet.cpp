// Copyright (c) 2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <interfaces/wallet.h>

#include <amount.h>
#include <coinjoin/client.h>
#include <interfaces/chain.h>
#include <interfaces/handler.h>
#include <policy/fees.h>
#include <primitives/transaction.h>
#include <rpc/server.h>
#include <script/standard.h>
#include <support/allocators/secure.h>
#include <sync.h>
#include <ui_interface.h>
#include <uint256.h>
#include <util/system.h>
#include <validation.h>
#include <wallet/context.h>
#include <wallet/fees.h>
#include <wallet/ismine.h>
#include <wallet/load.h>
#include <wallet/psbtwallet.h>
#include <wallet/rpcwallet.h>
#include <wallet/wallet.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace interfaces {
namespace {

//! Construct wallet tx struct.
WalletTx MakeWalletTx(CWallet& wallet, const CWalletTx& wtx)
{
    WalletTx result;
    bool fInputDenomFound{false}, fOutputDenomFound{false};
    result.tx = wtx.tx;
    result.txin_is_mine.reserve(wtx.tx->vin.size());
    for (const auto& txin : wtx.tx->vin) {
        result.txin_is_mine.emplace_back(wallet.IsMine(txin));
        if (!fInputDenomFound && result.txin_is_mine.back() && wallet.IsDenominated(txin.prevout)) {
            fInputDenomFound = true;
        }
    }
    result.txout_is_mine.reserve(wtx.tx->vout.size());
    result.txout_address.reserve(wtx.tx->vout.size());
    result.txout_address_is_mine.reserve(wtx.tx->vout.size());
    for (const auto& txout : wtx.tx->vout) {
        result.txout_is_mine.emplace_back(wallet.IsMine(txout));
        result.txout_address.emplace_back();
        result.txout_address_is_mine.emplace_back(ExtractDestination(txout.scriptPubKey, result.txout_address.back()) ?
                                                      wallet.IsMine(result.txout_address.back()) :
                                                      ISMINE_NO);
        if (!fOutputDenomFound && result.txout_address_is_mine.back() && CCoinJoin::IsDenominatedAmount(txout.nValue)) {
            fOutputDenomFound = true;
        }
    }
    result.credit = wtx.GetCredit(ISMINE_ALL);
    result.debit = wtx.GetDebit(ISMINE_ALL);
    result.change = wtx.GetChange();
    result.time = wtx.GetTxTime();
    result.value_map = wtx.mapValue;
    result.is_coinbase = wtx.IsCoinBase();
    // The determination of is_denominate is based on simplified checks here because in this part of the code
    // we only want to know about mixing transactions belonging to this specific wallet.
    result.is_denominate = wtx.tx->vin.size() == wtx.tx->vout.size() && // Number of inputs is same as number of outputs
                           (result.credit - result.debit) == 0 && // Transaction pays no tx fee
                           fInputDenomFound && fOutputDenomFound; // At least 1 input and 1 output are denominated belonging to the provided wallet
    return result;
}

//! Construct wallet tx status struct.
WalletTxStatus MakeWalletTxStatus(const CWallet& wallet, const CWalletTx& wtx)
{
    WalletTxStatus result;
    result.block_height = wallet.chain().getBlockHeight(wtx.m_confirm.hashBlock).value_or(std::numeric_limits<int>::max());
    result.blocks_to_maturity = wtx.GetBlocksToMaturity();
    result.depth_in_main_chain = wtx.GetDepthInMainChain();
    result.time_received = wtx.nTimeReceived;
    result.lock_time = wtx.tx->nLockTime;
    result.is_final = wallet.chain().checkFinalTx(*wtx.tx);
    result.is_trusted = wtx.IsTrusted();
    result.is_abandoned = wtx.isAbandoned();
    result.is_coinbase = wtx.IsCoinBase();
    result.is_in_main_chain = wtx.IsInMainChain();
    result.is_chainlocked = wtx.IsChainLocked();
    result.is_islocked = wtx.IsLockedByInstantSend();
    return result;
}

//! Construct wallet TxOut struct.
WalletTxOut MakeWalletTxOut(const CWallet& wallet,
    const CWalletTx& wtx,
    int n,
    int depth) EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet)
{
    WalletTxOut result;
    result.txout = wtx.tx->vout[n];
    result.time = wtx.GetTxTime();
    result.depth_in_main_chain = depth;
    result.is_spent = wallet.IsSpent(wtx.GetHash(), n);
    return result;
}

class CoinJoinImpl : public CoinJoin::Client
{
    std::shared_ptr<CCoinJoinClientManager> m_manager;
public:
    CoinJoinImpl(const std::shared_ptr<CWallet>& wallet) : m_manager(coinJoinClientManagers.at(wallet->GetName())) {}
    void resetCachedBlocks() override
    {
        m_manager->nCachedNumBlocks = std::numeric_limits<int>::max();
    }
    void resetPool() override
    {
        m_manager->ResetPool();
    }
    void disableAutobackups() override
    {
        m_manager->fCreateAutoBackups = false;
    }
    int getCachedBlocks() override
    {
        return m_manager->nCachedNumBlocks;
    }
    std::string getSessionDenoms() override
    {
        return m_manager->GetSessionDenoms();
    }
    void setCachedBlocks(int nCachedBlocks) override
    {
       m_manager->nCachedNumBlocks = nCachedBlocks;
    }
    bool isMixing() override
    {
        return m_manager->IsMixing();
    }
    bool startMixing() override
    {
        return m_manager->StartMixing();
    }
    void stopMixing() override
    {
        m_manager->StopMixing();
    }
};

class WalletImpl : public Wallet
{
public:

    explicit WalletImpl(const std::shared_ptr<CWallet>& wallet) : m_wallet(wallet), m_coinjoin(wallet) {}

    void markDirty() override
    {
        m_wallet->MarkDirty();
    }
    bool encryptWallet(const SecureString& wallet_passphrase) override
    {
        return m_wallet->EncryptWallet(wallet_passphrase);
    }
    bool isCrypted() override { return m_wallet->IsCrypted(); }
    bool lock(bool fAllowMixing) override { return m_wallet->Lock(fAllowMixing); }
    bool unlock(const SecureString& wallet_passphrase, bool fAllowMixing) override { return m_wallet->Unlock(wallet_passphrase, fAllowMixing); }
    bool isLocked(bool fForMixing) override { return m_wallet->IsLocked(fForMixing); }
    bool changeWalletPassphrase(const SecureString& old_wallet_passphrase,
        const SecureString& new_wallet_passphrase) override
    {
        return m_wallet->ChangeWalletPassphrase(old_wallet_passphrase, new_wallet_passphrase);
    }
    void abortRescan() override { m_wallet->AbortRescan(); }
    bool backupWallet(const std::string& filename) override { return m_wallet->BackupWallet(filename); }
    bool autoBackupWallet(const fs::path& wallet_path, bilingual_str& error_string, std::vector<bilingual_str>& warnings) override
    {
        return m_wallet->AutoBackupWallet(wallet_path, error_string, warnings);
    }
    int64_t getKeysLeftSinceAutoBackup() override { return m_wallet->nKeysLeftSinceAutoBackup; }
    std::string getWalletName() override { return m_wallet->GetName(); }
    bool getNewDestination(const std::string label, CTxDestination& dest) override
    {
        auto spk_man = m_wallet->GetLegacyScriptPubKeyMan();
        if (!spk_man) {
            return false;
        }
        std::string error;
        return m_wallet->GetNewDestination(label, dest, error);
    }
    bool getPubKey(const CScript& script, const CKeyID& address, CPubKey& pub_key) override
    {
        const SigningProvider* provider = m_wallet->GetSigningProvider(script);
        if (provider) {
            return provider->GetPubKey(address, pub_key);
        }
        return false;
    }
    bool getPrivKey(const CScript& script, const CKeyID& address, CKey& key) override
    {
        const SigningProvider* provider = m_wallet->GetSigningProvider(script);
        if (provider) {
            return provider->GetKey(address, key);
        }
        return false;
    }
    bool isSpendable(const CScript& script) override { return m_wallet->IsMine(script) & ISMINE_SPENDABLE; }
    bool isSpendable(const CTxDestination& dest) override { return m_wallet->IsMine(dest) & ISMINE_SPENDABLE; }
    bool haveWatchOnly() override
    {
        auto spk_man = m_wallet->GetLegacyScriptPubKeyMan();
        if (spk_man) {
            return spk_man->HaveWatchOnly();
        }
        return false;
    };
    bool setAddressBook(const CTxDestination& dest, const std::string& name, const std::string& purpose) override
    {
        return m_wallet->SetAddressBook(dest, name, purpose);
    }
    bool delAddressBook(const CTxDestination& dest) override
    {
        return m_wallet->DelAddressBook(dest);
    }
    bool getAddress(const CTxDestination& dest,
        std::string* name,
        isminetype* is_mine,
        std::string* purpose) override
    {
        LOCK(m_wallet->cs_wallet);
        auto it = m_wallet->mapAddressBook.find(dest);
        if (it == m_wallet->mapAddressBook.end()) {
            return false;
        }
        if (name) {
            *name = it->second.name;
        }
        if (is_mine) {
            *is_mine = m_wallet->IsMine(dest);
        }
        if (purpose) {
            *purpose = it->second.purpose;
        }
        return true;
    }
    std::vector<WalletAddress> getAddresses() override
    {
        LOCK(m_wallet->cs_wallet);
        std::vector<WalletAddress> result;
        for (const auto& item : m_wallet->mapAddressBook) {
            result.emplace_back(item.first, m_wallet->IsMine(item.first), item.second.name, item.second.purpose);
        }
        return result;
    }
    //void learnRelatedScripts(const CPubKey& key, OutputType type) override { if (spk_man) m_wallet->GetLegacyScriptPubKeyMan()->LearnRelatedScripts(key, type); }
    bool addDestData(const CTxDestination& dest, const std::string& key, const std::string& value) override
    {
        LOCK(m_wallet->cs_wallet);
        WalletBatch batch{m_wallet->GetDatabase()};
        return m_wallet->AddDestData(batch, dest, key, value);
    }
    bool eraseDestData(const CTxDestination& dest, const std::string& key) override
    {
        LOCK(m_wallet->cs_wallet);
        WalletBatch batch{m_wallet->GetDatabase()};
        return m_wallet->EraseDestData(batch, dest, key);
    }
    std::vector<std::string> getDestValues(const std::string& prefix) override
    {
        LOCK(m_wallet->cs_wallet);
        return m_wallet->GetDestValues(prefix);
    }
    void lockCoin(const COutPoint& output) override
    {
        LOCK(m_wallet->cs_wallet);
        return m_wallet->LockCoin(output);
    }
    void unlockCoin(const COutPoint& output) override
    {
        LOCK(m_wallet->cs_wallet);
        return m_wallet->UnlockCoin(output);
    }
    bool isLockedCoin(const COutPoint& output) override
    {
        LOCK(m_wallet->cs_wallet);
        return m_wallet->IsLockedCoin(output.hash, output.n);
    }
    void listLockedCoins(std::vector<COutPoint>& outputs) override
    {
        LOCK(m_wallet->cs_wallet);
        return m_wallet->ListLockedCoins(outputs);
    }
    void listProTxCoins(std::vector<COutPoint>& outputs) override
    {
        LOCK(m_wallet->cs_wallet);
        return m_wallet->ListProTxCoins(outputs);
    }
    CTransactionRef createTransaction(const std::vector<CRecipient>& recipients,
        const CCoinControl& coin_control,
        bool sign,
        int& change_pos,
        CAmount& fee,
        bilingual_str& fail_reason) override
    {
        LOCK(m_wallet->cs_wallet);
        ReserveDestination m_dest(m_wallet.get());
        CTransactionRef tx;
        if (!m_wallet->CreateTransaction(recipients, tx, fee, change_pos,
                fail_reason, coin_control, sign)) {
            return {};
        }
        return tx;
    }
    void commitTransaction(CTransactionRef tx,
        WalletValueMap value_map,
        WalletOrderForm order_form) override
    {
        LOCK2(m_wallet->cs_wallet, cs_main);
        ReserveDestination m_dest(m_wallet.get());
        m_wallet->CommitTransaction(std::move(tx), std::move(value_map), std::move(order_form));
    }
    bool transactionCanBeAbandoned(const uint256& txid) override { return m_wallet->TransactionCanBeAbandoned(txid); }
    bool transactionCanBeResent(const uint256& txid) override { return m_wallet->TransactionCanBeResent(txid); }
    bool abandonTransaction(const uint256& txid) override
    {
        LOCK(m_wallet->cs_wallet);
        return m_wallet->AbandonTransaction(txid);
    }
    bool resendTransaction(const uint256& txid) override
    {
        LOCK(m_wallet->cs_wallet);
        return m_wallet->ResendTransaction(txid);
    }
    CTransactionRef getTx(const uint256& txid) override
    {
        LOCK(m_wallet->cs_wallet);
        auto mi = m_wallet->mapWallet.find(txid);
        if (mi != m_wallet->mapWallet.end()) {
            return mi->second.tx;
        }
        return {};
    }
    WalletTx getWalletTx(const uint256& txid) override
    {
        LOCK(m_wallet->cs_wallet);
        auto mi = m_wallet->mapWallet.find(txid);
        if (mi != m_wallet->mapWallet.end()) {
            return MakeWalletTx(*m_wallet, mi->second);
        }
        return {};
    }
    std::vector<WalletTx> getWalletTxs() override
    {
        LOCK(m_wallet->cs_wallet);
        std::vector<WalletTx> result;
        result.reserve(m_wallet->mapWallet.size());
        for (const auto& entry : m_wallet->mapWallet) {
            result.emplace_back(MakeWalletTx(*m_wallet, entry.second));
        }
        return result;
    }
    bool tryGetTxStatus(const uint256& txid,
        interfaces::WalletTxStatus& tx_status,
        int64_t& block_time) override
    {
        TRY_LOCK(m_wallet->cs_wallet, locked_wallet);
        if (!locked_wallet) {
            return false;
        }
        auto mi = m_wallet->mapWallet.find(txid);
        if (mi == m_wallet->mapWallet.end()) {
            return false;
        }
        if (std::optional<int> height = m_wallet->chain().getHeight()) {
            block_time = m_wallet->chain().getBlockTime(*height);
        } else {
            block_time = -1;
        }
        tx_status = MakeWalletTxStatus(*m_wallet, mi->second);
        return true;
    }
    WalletTx getWalletTxDetails(const uint256& txid,
        WalletTxStatus& tx_status,
        WalletOrderForm& order_form,
        bool& in_mempool,
        int& num_blocks) override
    {
        LOCK(m_wallet->cs_wallet);
        auto mi = m_wallet->mapWallet.find(txid);
        if (mi != m_wallet->mapWallet.end()) {
            num_blocks = m_wallet->GetLastBlockHeight();
            in_mempool = mi->second.InMempool();
            order_form = mi->second.vOrderForm;
            tx_status = MakeWalletTxStatus(*m_wallet, mi->second);
            return MakeWalletTx(*m_wallet, mi->second);
        }
        return {};
    }
    int getRealOutpointCoinJoinRounds(const COutPoint& outpoint) override { return m_wallet->GetRealOutpointCoinJoinRounds(outpoint); }
    bool isFullyMixed(const COutPoint& outpoint) override { return m_wallet->IsFullyMixed(outpoint); }

    TransactionError fillPSBT(PartiallySignedTransaction& psbtx,
        bool& complete,
        int sighash_type = 1 /* SIGHASH_ALL */,
        bool sign = true,
        bool bip32derivs = false) override
    {
        return FillPSBT(m_wallet.get(), psbtx, complete, sighash_type, sign, bip32derivs);
    }
    WalletBalances getBalances() override
    {
        const auto bal = m_wallet->GetBalance();
        WalletBalances result;
        result.balance = bal.m_mine_trusted;
        result.unconfirmed_balance = bal.m_mine_untrusted_pending;
        result.immature_balance = bal.m_mine_immature;
        result.anonymized_balance = bal.m_anonymized;
        result.have_watch_only = haveWatchOnly();
        if (result.have_watch_only) {
            result.watch_only_balance = bal.m_watchonly_trusted;
            result.unconfirmed_watch_only_balance = bal.m_watchonly_untrusted_pending;
            result.immature_watch_only_balance = bal.m_watchonly_immature;
        }
        return result;
    }
    bool tryGetBalances(WalletBalances& balances, int& num_blocks) override
    {
        TRY_LOCK(m_wallet->cs_wallet, locked_wallet);
        if (!locked_wallet) {
            return false;
        }
        balances = getBalances();
        num_blocks = m_wallet->chain().getHeight().value_or(-1);
        return true;
    }
    CAmount getBalance() override
    {
        return m_wallet->GetBalance().m_mine_trusted;
    }
    CAmount getAnonymizableBalance(bool fSkipDenominated, bool fSkipUnconfirmed) override
    {
        return m_wallet->GetAnonymizableBalance(fSkipDenominated, fSkipUnconfirmed);
    }
    CAmount getAnonymizedBalance() override
    {
        return m_wallet->GetBalance().m_anonymized;
    }
    CAmount getDenominatedBalance(bool unconfirmed) override
    {
        const auto bal = m_wallet->GetBalance();
        if (unconfirmed) {
            return bal.m_denominated_untrusted_pending;
        } else {
            return bal.m_denominated_trusted;
        }
    }
    CAmount getNormalizedAnonymizedBalance() override
    {
        return m_wallet->GetNormalizedAnonymizedBalance();
    }
    CAmount getAverageAnonymizedRounds() override
    {
        return m_wallet->GetAverageAnonymizedRounds();
    }
    CAmount getAvailableBalance(const CCoinControl& coin_control) override
    {
        if (coin_control.IsUsingCoinJoin()) {
            return m_wallet->GetBalance(0, coin_control.m_avoid_address_reuse, false, &coin_control).m_anonymized;
        } else {
            return m_wallet->GetAvailableBalance(&coin_control);
        }
    }
    isminetype txinIsMine(const CTxIn& txin) override
    {
        LOCK(m_wallet->cs_wallet);
        return m_wallet->IsMine(txin);
    }
    isminetype txoutIsMine(const CTxOut& txout) override
    {
        LOCK(m_wallet->cs_wallet);
        return m_wallet->IsMine(txout);
    }
    CAmount getDebit(const CTxIn& txin, isminefilter filter) override
    {
        LOCK(m_wallet->cs_wallet);
        return m_wallet->GetDebit(txin, filter);
    }
    CAmount getCredit(const CTxOut& txout, isminefilter filter) override
    {
        LOCK(m_wallet->cs_wallet);
        return m_wallet->GetCredit(txout, filter);
    }
    CoinsList listCoins() override
    {
        LOCK(m_wallet->cs_wallet);
        CoinsList result;
        for (const auto& entry : m_wallet->ListCoins()) {
            auto& group = result[entry.first];
            for (const auto& coin : entry.second) {
                group.emplace_back(COutPoint(coin.tx->GetHash(), coin.i),
                    MakeWalletTxOut(*m_wallet, *coin.tx, coin.i, coin.nDepth));
            }
        }
        return result;
    }
    std::vector<WalletTxOut> getCoins(const std::vector<COutPoint>& outputs) override
    {
        LOCK(m_wallet->cs_wallet);
        std::vector<WalletTxOut> result;
        result.reserve(outputs.size());
        for (const auto& output : outputs) {
            result.emplace_back();
            auto it = m_wallet->mapWallet.find(output.hash);
            if (it != m_wallet->mapWallet.end()) {
                int depth = it->second.GetDepthInMainChain();
                if (depth >= 0) {
                    result.back() = MakeWalletTxOut(*m_wallet, it->second, output.n, depth);
                }
            }
        }
        return result;
    }
    CAmount getRequiredFee(unsigned int tx_bytes) override { return GetRequiredFee(*m_wallet, tx_bytes); }
    CAmount getMinimumFee(unsigned int tx_bytes,
        const CCoinControl& coin_control,
        int* returned_target,
        FeeReason* reason) override
    {
        FeeCalculation fee_calc;
        CAmount result;
        result = GetMinimumFee(*m_wallet, tx_bytes, coin_control, &fee_calc);
        if (returned_target) *returned_target = fee_calc.returnedTarget;
        if (reason) *reason = fee_calc.reason;
        return result;
    }
    unsigned int getConfirmTarget() override { return m_wallet->m_confirm_target; }
    bool hdEnabled() override { return m_wallet->IsHDEnabled(); }
    bool canGetAddresses() override { return m_wallet->CanGetAddresses(); }
    bool IsWalletFlagSet(uint64_t flag) override { return m_wallet->IsWalletFlagSet(flag); }
    CoinJoin::Client& coinJoin() override { return m_coinjoin; }
    CAmount getDefaultMaxTxFee() override { return m_wallet->m_default_max_tx_fee; }
    void remove() override
    {
        RemoveWallet(m_wallet, false /* load_on_start */);
    }
    std::unique_ptr<Handler> handleUnload(UnloadFn fn) override
    {
        return MakeHandler(m_wallet->NotifyUnload.connect(fn));
    }
    std::unique_ptr<Handler> handleShowProgress(ShowProgressFn fn) override
    {
        return MakeHandler(m_wallet->ShowProgress.connect(fn));
    }
    std::unique_ptr<Handler> handleStatusChanged(StatusChangedFn fn) override
    {
        return MakeHandler(m_wallet->NotifyStatusChanged.connect([fn](CWallet*) { fn(); }));
    }
    std::unique_ptr<Handler> handleAddressBookChanged(AddressBookChangedFn fn) override
    {
        return MakeHandler(m_wallet->NotifyAddressBookChanged.connect(
            [fn](CWallet*, const CTxDestination& address, const std::string& label, bool is_mine,
                const std::string& purpose, ChangeType status) { fn(address, label, is_mine, purpose, status); }));
    }
    std::unique_ptr<Handler> handleTransactionChanged(TransactionChangedFn fn) override
    {
        return MakeHandler(m_wallet->NotifyTransactionChanged.connect(
            [fn](CWallet*, const uint256& txid, ChangeType status) { fn(txid, status); }));
    }
    std::unique_ptr<Handler> handleInstantLockReceived(InstantLockReceivedFn fn) override
    {
        return MakeHandler(m_wallet->NotifyISLockReceived.connect(
            [fn]() { fn(); }));
    }
    std::unique_ptr<Handler> handleChainLockReceived(ChainLockReceivedFn fn) override
    {
        return MakeHandler(m_wallet->NotifyChainLockReceived.connect(
            [fn](int chainLockHeight) { fn(chainLockHeight); }));
    }
    std::unique_ptr<Handler> handleWatchOnlyChanged(WatchOnlyChangedFn fn) override
    {
        return MakeHandler(m_wallet->NotifyWatchonlyChanged.connect(fn));
    }
    std::unique_ptr<Handler> handleCanGetAddressesChanged(CanGetAddressesChangedFn fn) override
    {
        return MakeHandler(m_wallet->NotifyCanGetAddressesChanged.connect(fn));
    }
    CWallet* wallet() override { return m_wallet.get(); }

    std::shared_ptr<CWallet> m_wallet;
    CoinJoinImpl m_coinjoin;
};

class WalletClientImpl : public WalletClient
{
public:
    WalletClientImpl(Chain& chain, ArgsManager& args)
    {
        m_context.chain = &chain;
        m_context.args = &args;
    }
    ~WalletClientImpl() override { UnloadWallets(); }

    //! ChainClient methods
    void registerRpcs() override
    {
        for (const CRPCCommand& command : GetWalletRPCCommands()) {
            m_rpc_commands.emplace_back(command.category, command.name, [this, &command](const JSONRPCRequest& request, UniValue& result, bool last_handler) {
                return command.actor({request, m_context}, result, last_handler);
            }, command.argNames, command.unique_id);
            m_rpc_handlers.emplace_back(m_context.chain->handleRpc(m_rpc_commands.back()));
        }
    }
    bool verify() override { return VerifyWallets(*m_context.chain); }
    bool load() override { return LoadWallets(*m_context.chain); }
    void start(CScheduler& scheduler) override { return StartWallets(scheduler, *Assert(m_context.args)); }
    void flush() override { return FlushWallets(); }
    void stop() override { return StopWallets(); }
    void setMockTime(int64_t time) override { return SetMockTime(time); }

    //! WalletClient methods
    std::unique_ptr<Wallet> createWallet(const std::string& name, const SecureString& passphrase, uint64_t wallet_creation_flags, bilingual_str& error, std::vector<bilingual_str>& warnings) override
    {
        std::shared_ptr<CWallet> wallet;
        DatabaseOptions options;
        DatabaseStatus status;
        options.require_create = true;
        options.create_flags = wallet_creation_flags;
        options.create_passphrase = passphrase;
        return MakeWallet(CreateWallet(*m_context.chain, name, true /* load_on_start */, options, status, error, warnings));
    }
    std::unique_ptr<Wallet> loadWallet(const std::string& name, bilingual_str& error, std::vector<bilingual_str>& warnings) override
    {
        DatabaseOptions options;
        DatabaseStatus status;
        options.require_existing = true;
        return MakeWallet(LoadWallet(*m_context.chain, name, true /* load_on_start */, options, status, error, warnings));
    }
    std::string getWalletDir() override
    {
        return GetWalletDir().string();
    }
    std::vector<std::string> listWalletDir() override
    {
        std::vector<std::string> paths;
        for (auto& path : ListDatabases(GetWalletDir())) {
            paths.push_back(path.string());
        }
        return paths;
    }
    std::vector<std::unique_ptr<Wallet>> getWallets() override
    {
        std::vector<std::unique_ptr<Wallet>> wallets;
        for (const auto& wallet : GetWallets()) {
            wallets.emplace_back(MakeWallet(wallet));
        }
        return wallets;
    }
    std::unique_ptr<Handler> handleLoadWallet(LoadWalletFn fn) override
    {
        return HandleLoadWallet(std::move(fn));
    }

    WalletContext m_context;
    const std::vector<std::string> m_wallet_filenames;
    std::vector<std::unique_ptr<Handler>> m_rpc_handlers;
    std::list<CRPCCommand> m_rpc_commands;
};

} // namespace

std::unique_ptr<Wallet> MakeWallet(const std::shared_ptr<CWallet>& wallet) { return wallet ? std::make_unique<WalletImpl>(wallet) : nullptr; }

std::unique_ptr<WalletClient> MakeWalletClient(Chain& chain, ArgsManager& args)
{
    return std::make_unique<WalletClientImpl>(chain, args);
}

} // namespace interfaces
