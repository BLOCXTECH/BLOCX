// Copyright (c) 2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <logging.h>
#include <util/system.h>
#include <walletinitinterface.h>

class CWallet;

namespace interfaces {
class Chain;
class Handler;
class Wallet;
}

class DummyWalletInit : public WalletInitInterface {
public:

    bool HasWalletSupport() const override {return false;}
    void AddWalletOptions(ArgsManager& argsman) const override;
    bool ParameterInteraction() const override {return true;}
    void Construct(NodeContext& node) const override {LogPrintf("No wallet support compiled in!\n");}

    // BLOCX Specific WalletInitInterface InitCoinJoinSettings
    void AutoLockMasternodeCollaterals() const override {}
    void InitCoinJoinSettings() const override {}
    bool InitAutoBackup() const override {return true;}
};

void DummyWalletInit::AddWalletOptions(ArgsManager& argsman) const
{
    argsman.AddHiddenArgs({
        "-avoidpartialspends",
        "-createwalletbackups=<n>",
        "-disablewallet",
        "-instantsendnotify=<cmd>",
        "-keypool=<n>",
        "-maxtxfee=<amt>",
        "-rescan=<mode>",
        "-salvagewallet",
        "-spendzeroconfchange",
        "-wallet=<path>",
        "-walletbackupsdir=<dir>",
        "-walletbroadcast",
        "-walletdir=<dir>",
        "-walletnotify=<cmd>",
        "-discardfee=<amt>",
        "-fallbackfee=<amt>",
        "-mintxfee=<amt>",
        "-paytxfee=<amt>",
        "-txconfirmtarget=<n>",
        "-hdseed=<hex>",
        "-mnemonic=<text>",
        "-mnemonicpassphrase=<text>",
        "-usehd",
        "-enablecoinjoin",
        "-coinjoinamount=<n>",
        "-coinjoinautostart",
        "-coinjoindenomsgoal=<n>",
        "-coinjoindenomshardcap=<n>",
        "-coinjoinmultisession",
        "-coinjoinrounds=<n>",
        "-coinjoinsessions=<n>",
        "-dblogsize=<n>",
        "-flushwallet",
        "-privdb",
        "-walletrejectlongchains",
        "-unsafesqlitesync"
    });
}

const WalletInitInterface& g_wallet_init_interface = DummyWalletInit();

namespace interfaces {

std::unique_ptr<Wallet> MakeWallet(const std::shared_ptr<CWallet>& wallet)
{
    throw std::logic_error("Wallet function called in non-wallet build.");
}

} // namespace interfaces
