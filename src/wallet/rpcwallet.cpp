// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Copyright (c) 2014-2023 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <amount.h>
#include <chainparams.h>
#include <core_io.h>
#include <httpserver.h>
#include <interfaces/chain.h>
#include <node/context.h>
#include <policy/feerate.h>
#include <policy/fees.h>
#include <rpc/blockchain.h>
#include <rpc/rawtransaction_util.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <script/descriptor.h>
#include <util/bip32.h>
#include <util/fees.h>
#include <util/message.h> // For MessageSign()
#include <util/moneystr.h>
#include <util/string.h>
#include <util/system.h>
#include <util/translation.h>
#include <util/url.h>
#include <util/vector.h>
#include <validation.h>
#include <wallet/coincontrol.h>
#include <wallet/context.h>
#include <wallet/psbtwallet.h>
#include <wallet/load.h>
#include <wallet/rpcwallet.h>
#include <wallet/scriptpubkeyman.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>
#include <wallet/walletutil.h>
#include <key_io.h>

#include <coinjoin/client.h>
#include <coinjoin/options.h>
#include <llmq/chainlocks.h>
#include <llmq/instantsend.h>

#include <stdint.h>

#include <univalue.h>


static const std::string WALLET_ENDPOINT_BASE = "/wallet/";

static inline bool GetAvoidReuseFlag(CWallet * const pwallet, const UniValue& param) {
    bool can_avoid_reuse = pwallet->IsWalletFlagSet(WALLET_FLAG_AVOID_REUSE);
    bool avoid_reuse = param.isNull() ? can_avoid_reuse : param.get_bool();

    if (avoid_reuse && !can_avoid_reuse) {
        throw JSONRPCError(RPC_WALLET_ERROR, "wallet does not have the \"avoid reuse\" feature enabled");
    }

    return avoid_reuse;
}


/** Used by RPC commands that have an include_watchonly parameter.
 *  We default to true for watchonly wallets if include_watchonly isn't
 *  explicitly set.
 */
static bool ParseIncludeWatchonly(const UniValue& include_watchonly, const CWallet& pwallet)
{
    if (include_watchonly.isNull()) {
        // if include_watchonly isn't explicitly set, then check if we have a watchonly wallet
        return pwallet.IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS);
    }

    // otherwise return whatever include_watchonly was set to
    return include_watchonly.get_bool();
}


/** Checks if a CKey is in the given CWallet compressed or otherwise*/
/*
bool HaveKey(const SigningProvider& wallet, const CKey& key)
{
    CKey key2;
    key2.Set(key.begin(), key.end(), !key.IsCompressed());
    return wallet.HaveKey(key.GetPubKey().GetID()) || wallet.HaveKey(key2.GetPubKey().GetID());
}
*/

bool GetWalletNameFromJSONRPCRequest(const JSONRPCRequest& request, std::string& wallet_name)
{
    if (request.URI.substr(0, WALLET_ENDPOINT_BASE.size()) == WALLET_ENDPOINT_BASE) {
        // wallet endpoint was used
        wallet_name = urlDecode(request.URI.substr(WALLET_ENDPOINT_BASE.size()));
        return true;
    }
    return false;
}

std::shared_ptr<CWallet> GetWalletForJSONRPCRequest(const JSONRPCRequest& request)
{
    std::string wallet_name;
    if (GetWalletNameFromJSONRPCRequest(request, wallet_name)) {
        std::shared_ptr<CWallet> pwallet = GetWallet(wallet_name);
        if (!pwallet) throw JSONRPCError(RPC_WALLET_NOT_FOUND, "Requested wallet does not exist or is not loaded");
        return pwallet;
    }

    std::vector<std::shared_ptr<CWallet>> wallets = GetWallets();
    if (wallets.size() == 1 || (request.fHelp && wallets.size() > 0)) {
        return wallets[0];
    }

    if (request.fHelp) return nullptr;

    if (wallets.empty()) {
        throw JSONRPCError(
            RPC_WALLET_NOT_FOUND, "No wallet is loaded. Load a wallet using loadwallet or create a new one with createwallet. (Note: A default wallet is no longer automatically created)");
    }
    throw JSONRPCError(RPC_WALLET_NOT_SPECIFIED,
        "Wallet file not specified (must request wallet RPC through /wallet/<filename> uri-path).");
}

void EnsureWalletIsUnlocked(const CWallet* pwallet)
{
    if (pwallet->IsLocked()) {
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first.");
    }
}

LegacyScriptPubKeyMan& EnsureLegacyScriptPubKeyMan(CWallet& wallet)
{
    LegacyScriptPubKeyMan* spk_man = wallet.GetLegacyScriptPubKeyMan();
    if (!spk_man) {
        throw JSONRPCError(RPC_WALLET_ERROR, "This type of wallet does not support this command");
    }
    return *spk_man;
}

WalletContext& EnsureWalletContext(const CoreContext& context)
{
    auto* wallet_context = GetContext<WalletContext>(context);
    if (!wallet_context) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Wallet context not found");
    }
    return *wallet_context;
}

static void WalletTxToJSON(interfaces::Chain& chain, const CWalletTx& wtx, UniValue& entry)
{
    int confirms = wtx.GetDepthInMainChain();
    bool fLocked = llmq::quorumInstantSendManager->IsLocked(wtx.GetHash());
    bool chainlock = false;
    if (confirms > 0) {
        chainlock = wtx.IsChainLocked();
    }
    entry.pushKV("confirmations", confirms);
    entry.pushKV("instantlock", fLocked || chainlock);
    entry.pushKV("instantlock_internal", fLocked);
    entry.pushKV("chainlock", chainlock);
    if (wtx.IsCoinBase())
        entry.pushKV("generated", true);
    if (confirms > 0)
    {
        entry.pushKV("blockhash", wtx.m_confirm.hashBlock.GetHex());
        entry.pushKV("blockindex", wtx.m_confirm.nIndex);
        int64_t block_time;
        bool found_block = chain.findBlock(wtx.m_confirm.hashBlock, nullptr /* block */, &block_time);
        CHECK_NONFATAL(found_block);
        entry.pushKV("blocktime", block_time);
    } else {
        entry.pushKV("trusted", wtx.IsTrusted());
    }
    uint256 hash = wtx.GetHash();
    entry.pushKV("txid", hash.GetHex());
    UniValue conflicts(UniValue::VARR);
    for (const uint256& conflict : wtx.GetConflicts())
        conflicts.push_back(conflict.GetHex());
    entry.pushKV("walletconflicts", conflicts);
    entry.pushKV("time", wtx.GetTxTime());
    entry.pushKV("timereceived", (int64_t)wtx.nTimeReceived);

    for (const std::pair<const std::string, std::string>& item : wtx.mapValue)
        entry.pushKV(item.first, item.second);
}

static std::string LabelFromValue(const UniValue& value)
{
    std::string label = value.get_str();
    if (label == "*")
        throw JSONRPCError(RPC_WALLET_INVALID_LABEL_NAME, "Invalid label name");
    return label;
}

UniValue getnewaddress(const JSONRPCRequest& request)
{
    RPCHelpMan{"getnewaddress",
        "\nReturns a new BLOCX address for receiving payments.\n"
        "If 'label' is specified, it is added to the address book \n"
        "so payments received with the address will be associated with 'label'.\n",
        {
            {"label", RPCArg::Type::STR, /* default */ "\"\"", "The label name for the address to be linked to. It can also be set to the empty string \"\" to represent the default label. The label does not need to exist, it will be created if there is no label by the given name."},
        },
        RPCResult{
    RPCResult::Type::STR, "address", "The new blocx address"
        },
        RPCExamples{
            HelpExampleCli("getnewaddress", "")
    + HelpExampleRpc("getnewaddress", "")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    LegacyScriptPubKeyMan* spk_man = pwallet->GetLegacyScriptPubKeyMan();
    if (!spk_man) {
        throw JSONRPCError(RPC_WALLET_ERROR, "This type of wallet does not support this command");
    }
    LOCK(pwallet->cs_wallet);

    if (!pwallet->CanGetAddresses()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: This wallet has no available keys");
    }

    // Parse the label first so we don't generate a key if there's an error
    std::string label;
    if (!request.params[0].isNull())
        label = LabelFromValue(request.params[0]);

    CTxDestination dest;
    std::string error;
    if (!pwallet->GetNewDestination(label, dest, error)) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, error);
    }
    return EncodeDestination(dest);
}

UniValue getrawchangeaddress(const JSONRPCRequest& request)
{
    RPCHelpMan{"getrawchangeaddress",
        "\nReturns a new BLOCX address, for receiving change.\n"
        "This is for use with raw transactions, NOT normal use.\n",
        {},
        RPCResult{
            RPCResult::Type::STR, "address", "The address"
        },
        RPCExamples{
            HelpExampleCli("getrawchangeaddress", "")
    + HelpExampleRpc("getrawchangeaddress", "")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    LOCK(pwallet->cs_wallet);

    if (!pwallet->CanGetAddresses(true)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: This wallet has no available keys");
    }

    CTxDestination dest;
    std::string error;
    if (!pwallet->GetNewChangeDestination(dest, error)) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, error);
    }
    return EncodeDestination(dest);
}


static UniValue setlabel(const JSONRPCRequest& request)
{
    RPCHelpMan{"setlabel",
        "\nSets the label associated with the given address.\n",
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The blocx address to be associated with a label."},
            {"label", RPCArg::Type::STR, RPCArg::Optional::NO, "The label to assign to the address."},
        },
        RPCResult{RPCResult::Type::NONE, "", ""},
        RPCExamples{
            HelpExampleCli("setlabel", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwG\" \"tabby\"")
    + HelpExampleRpc("setlabel", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwG\", \"tabby\"")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    LOCK(pwallet->cs_wallet);

    CTxDestination dest = DecodeDestination(request.params[0].get_str());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid BLOCX address");
    }

    std::string label = LabelFromValue(request.params[1]);

    if (pwallet->IsMine(dest)) {
        pwallet->SetAddressBook(dest, label, "receive");
    } else {
        pwallet->SetAddressBook(dest, label, "send");
    }

    return NullUniValue;
}


static CTransactionRef SendMoney(CWallet* const pwallet, const CTxDestination& address, CAmount nValue, bool fSubtractFeeFromAmount, const CCoinControl& coin_control, mapValue_t mapValue)
{
    CAmount curBalance = pwallet->GetBalance(0, coin_control.m_avoid_address_reuse).m_mine_trusted;

    // Check amount
    if (nValue <= 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid amount");

    if (nValue > curBalance)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds");

    if (pwallet->GetBroadcastTransactions() && !pwallet->chain().p2pEnabled()) {
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");
    }

    if (coin_control.IsUsingCoinJoin()) {
        mapValue["DS"] = "1";
    }

    // Parse BLOCX address
    CScript scriptPubKey = GetScriptForDestination(address);

    // Create and send the transaction
    CAmount nFeeRequired = 0;
    bilingual_str error;
    std::vector<CRecipient> vecSend;
    int nChangePosRet = -1;
    CRecipient recipient = {scriptPubKey, nValue, fSubtractFeeFromAmount};
    vecSend.push_back(recipient);
    CTransactionRef tx;
    if (!pwallet->CreateTransaction(vecSend, tx, nFeeRequired, nChangePosRet, error, coin_control)) {
        if (!fSubtractFeeFromAmount && nValue + nFeeRequired > curBalance)
            error = strprintf(Untranslated("Error: This transaction requires a transaction fee of at least %s"), FormatMoney(nFeeRequired));
        throw JSONRPCError(RPC_WALLET_ERROR, error.original);
    }
    pwallet->CommitTransaction(tx, std::move(mapValue), {} /* orderForm */);
    return tx;
}

static UniValue sendtoaddress(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    RPCHelpMan{"sendtoaddress",
        "\nSend an amount to a given address." +
                HELP_REQUIRING_PASSPHRASE,
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The blocx address to send to."},
            {"amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "The amount in " + CURRENCY_UNIT + " to send. eg 0.1"},
            {"comment", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, "A comment used to store what the transaction is for.\n"
    "                             This is not part of the transaction, just kept in your wallet."},
            {"comment_to", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, "A comment to store the name of the person or organization\n"
    "                             to which you're sending the transaction. This is not part of the \n"
    "                             transaction, just kept in your wallet."},
            {"subtractfeefromamount", RPCArg::Type::BOOL, /* default */ "false", "The fee will be deducted from the amount being sent.\n"
    "                             The recipient will receive less amount of BLOCX than you enter in the amount field."},
            {"use_is", RPCArg::Type::BOOL, /* default */ "false", "Deprecated and ignored"},
            {"use_cj", RPCArg::Type::BOOL, /* default */ "false", "Use CoinJoin funds only"},
            {"conf_target", RPCArg::Type::NUM, /* default */ "wallet default", "Confirmation target (in blocks)"},
            {"estimate_mode", RPCArg::Type::STR, /* default */ "UNSET", "The fee estimate mode, must be one of:\n"
    "       \"UNSET\"\n"
    "       \"ECONOMICAL\"\n"
    "       \"CONSERVATIVE\""},
            {"avoid_reuse", RPCArg::Type::BOOL, /* default */ "true", "(only available if avoid_reuse wallet flag is set) Avoid spending from dirty addresses; addresses are considered\n"
    "                             dirty if they have previously been used in a transaction."},
        },
        RPCResult{
            RPCResult::Type::STR_HEX, "txid", "The transaction id."
        },
        RPCExamples{
            HelpExampleCli("sendtoaddress", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwG\" 0.1")
    + HelpExampleCli("sendtoaddress", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwG\" 0.1 \"donation\" \"seans outpost\"")
    + HelpExampleCli("sendtoaddress", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwG\" 0.1 \"\" \"\" true")
    + HelpExampleRpc("sendtoaddress", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwG\", 0.1, \"donation\", \"seans outpost\"")
        },
    }.Check(request);

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);

    CTxDestination dest = DecodeDestination(request.params[0].get_str());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    // Amount
    CAmount nAmount = AmountFromValue(request.params[1]);
    if (nAmount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");

    // Wallet comments
    mapValue_t mapValue;
    if (!request.params[2].isNull() && !request.params[2].get_str().empty())
        mapValue["comment"] = request.params[2].get_str();
    if (!request.params[3].isNull() && !request.params[3].get_str().empty())
        mapValue["to"] = request.params[3].get_str();

    bool fSubtractFeeFromAmount = false;
    if (!request.params[4].isNull()) {
        fSubtractFeeFromAmount = request.params[4].get_bool();
    }

    CCoinControl coin_control;

    if (!request.params[6].isNull()) {
        coin_control.UseCoinJoin(request.params[6].get_bool());
    }

    if (!request.params[7].isNull()) {
        coin_control.m_confirm_target = ParseConfirmTarget(request.params[7], pwallet->chain().estimateMaxBlocks());
    }

    if (!request.params[8].isNull()) {
        if (!FeeModeFromString(request.params[8].get_str(), coin_control.m_fee_mode)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid estimate_mode parameter");
        }
    }

    coin_control.m_avoid_address_reuse = GetAvoidReuseFlag(pwallet, request.params[9]);
    // We also enable partial spend avoidance if reuse avoidance is set.
    coin_control.m_avoid_partial_spends |= coin_control.m_avoid_address_reuse;

    EnsureWalletIsUnlocked(pwallet);

    CTransactionRef tx = SendMoney(pwallet, dest, nAmount, fSubtractFeeFromAmount, coin_control, std::move(mapValue));
    return tx->GetHash().GetHex();
}

// DEPRECATED
static UniValue instantsendtoaddress(const JSONRPCRequest& request)
{
    if (request.fHelp) {
        throw std::runtime_error("instantsendtoaddress is deprecated and sendtoaddress should be used instead");
    }
    LogPrintf("WARNING: Used deprecated RPC method 'instantsendtoaddress'! Please use 'sendtoaddress' instead\n");
    return sendtoaddress(request);
}

static UniValue listaddressgroupings(const JSONRPCRequest& request)
{
    RPCHelpMan{"listaddressgroupings",
        "\nLists groups of addresses which have had their common ownership\n"
        "made public by common use as inputs or as the resulting change\n"
        "in past transactions\n",
        {},
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {
                {RPCResult::Type::ARR, "", "",
                {
                    {RPCResult::Type::ARR, "", "",
                    {
                        {RPCResult::Type::STR, "address", "The blocx address"},
                        {RPCResult::Type::STR_AMOUNT, "amount", "The amount in " + CURRENCY_UNIT},
                        {RPCResult::Type::STR, "label", /* optional */ true, "The label"},
                    }},
                }},
            }},
        RPCExamples{
            HelpExampleCli("listaddressgroupings", "")
    + HelpExampleRpc("listaddressgroupings", "")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);

    UniValue jsonGroupings(UniValue::VARR);
    std::map<CTxDestination, CAmount> balances = pwallet->GetAddressBalances();
    for (const std::set<CTxDestination>& grouping : pwallet->GetAddressGroupings()) {
        UniValue jsonGrouping(UniValue::VARR);
        for (const CTxDestination& address : grouping)
        {
            UniValue addressInfo(UniValue::VARR);
            addressInfo.push_back(EncodeDestination(address));
            addressInfo.push_back(ValueFromAmount(balances[address]));
            {
                if (pwallet->mapAddressBook.find(address) != pwallet->mapAddressBook.end()) {
                    addressInfo.push_back(pwallet->mapAddressBook.find(address)->second.name);
                }
            }
            jsonGrouping.push_back(addressInfo);
        }
        jsonGroupings.push_back(jsonGrouping);
    }
    return jsonGroupings;
}

static UniValue listaddressbalances(const JSONRPCRequest& request)
{
    RPCHelpMan{"listaddressbalances",
        "\nLists addresses of this wallet and their balances\n",
        {
            {"minamount", RPCArg::Type::NUM, /* default */ "0", "Minimum balance in " + CURRENCY_UNIT + " an address should have to be shown in the list"},
        },
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {
                {RPCResult::Type::STR_AMOUNT, "amount", "The blocx address and the amount in " + CURRENCY_UNIT},
            }
        },
        RPCExamples{
            HelpExampleCli("listaddressbalances", "")
    + HelpExampleCli("listaddressbalances", "10")
    + HelpExampleRpc("listaddressbalances", "")
    + HelpExampleRpc("listaddressbalances", "10")
        }
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    LOCK(pwallet->cs_wallet);

    CAmount nMinAmount = 0;
    if (!request.params[0].isNull())
        nMinAmount = AmountFromValue(request.params[0]);

    if (nMinAmount < 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");

    UniValue jsonBalances(UniValue::VOBJ);
    std::map<CTxDestination, CAmount> balances = pwallet->GetAddressBalances();
    for (auto& balance : balances)
        if (balance.second >= nMinAmount)
            jsonBalances.pushKV(EncodeDestination(balance.first), ValueFromAmount(balance.second));

    return jsonBalances;
}

static UniValue signmessage(const JSONRPCRequest& request)
{
    RPCHelpMan{"signmessage",
        "\nSign a message with the private key of an address" +
                HELP_REQUIRING_PASSPHRASE,
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The blocx address to use for the private key."},
            {"message", RPCArg::Type::STR, RPCArg::Optional::NO, "The message to create a signature of."},
        },
        RPCResult{
            RPCResult::Type::STR, "signature", "The signature of the message encoded in base 64"
        },
        RPCExamples{
    "\nUnlock the wallet for 30 seconds\n"
    + HelpExampleCli("walletpassphrase", "\"mypassphrase\" 30") +
    "\nCreate the signature\n"
    + HelpExampleCli("signmessage", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwG\" \"my message\"") +
    "\nVerify the signature\n"
    + HelpExampleCli("verifymessage", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwG\" \"signature\" \"my message\"") +
    "\nAs a JSON-RPC call\n"
    + HelpExampleRpc("signmessage", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwG\", \"my message\"")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    LOCK(pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    std::string strAddress = request.params[0].get_str();
    std::string strMessage = request.params[1].get_str();

    CTxDestination dest = DecodeDestination(strAddress);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address");
    }

    const PKHash *pkhash = std::get_if<PKHash>(&dest);
    if (!pkhash) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to key");
    }

    CScript script_pub_key = GetScriptForDestination(*pkhash);
    const SigningProvider* provider = pwallet->GetSigningProvider(script_pub_key);
    if (!provider) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key not available");
    }

    CKey key;
    if (!provider->GetKey(CKeyID(*pkhash), key)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key not available");
    }

    std::string signature;

    if (!MessageSign(key, strMessage, signature)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Sign failed");
    }

    return signature;
}

static UniValue getreceivedbyaddress(const JSONRPCRequest& request)
{
    RPCHelpMan{"getreceivedbyaddress",
        "\nReturns the total amount received by the given address in transactions with at least minconf confirmations.\n",
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The blocx address for transactions."},
            {"minconf", RPCArg::Type::NUM, /* default */ "1", "Only include transactions confirmed at least this many times."},
            {"addlocked", RPCArg::Type::BOOL, /* default */ "false", "Whether to include transactions locked via InstantSend."},
        },
        RPCResult{
            RPCResult::Type::STR_AMOUNT, "amount", "The total amount in " + CURRENCY_UNIT + " received at this address."
        },
        RPCExamples{
    "\nThe amount from transactions with at least 1 confirmation\n"
    + HelpExampleCli("getreceivedbyaddress", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwG\"") +
    "\nThe amount including unconfirmed transactions, zero confirmations\n"
    + HelpExampleCli("getreceivedbyaddress", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwG\" 0") +
    "\nThe amount with at least 6 confirmations\n"
    + HelpExampleCli("getreceivedbyaddress", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwG\" 6") +
    "\nAs a JSON-RPC call\n"
    + HelpExampleRpc("getreceivedbyaddress", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwG\", 6")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);

    // BLOCX address
    CTxDestination dest = DecodeDestination(request.params[0].get_str());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid BLOCX address");
    }
    CScript scriptPubKey = GetScriptForDestination(dest);
    if (!pwallet->IsMine(scriptPubKey)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Address not found in wallet");
    }

    // Minimum confirmations
    int nMinDepth = 1;
    if (!request.params[1].isNull())
        nMinDepth = request.params[1].get_int();
    bool fAddLocked = (!request.params[2].isNull() && request.params[2].get_bool());

    // Tally
    CAmount nAmount = 0;
    for (const std::pair<const uint256, CWalletTx>& pairWtx : pwallet->mapWallet) {
        const CWalletTx& wtx = pairWtx.second;
        if (wtx.IsCoinBase() || !pwallet->chain().checkFinalTx(*wtx.tx)) {
            continue;
        }

        for (const CTxOut& txout : wtx.tx->vout)
            if (txout.scriptPubKey == scriptPubKey)
                if ((wtx.GetDepthInMainChain() >= nMinDepth) || (fAddLocked && wtx.IsLockedByInstantSend()))
                    nAmount += txout.nValue;
    }

    return  ValueFromAmount(nAmount);
}


static UniValue getreceivedbylabel(const JSONRPCRequest& request)
{
    RPCHelpMan{"getreceivedbylabel",
        "\nReturns the total amount received by addresses with <label> in transactions with specified minimum number of confirmations.\n",
        {
            {"label", RPCArg::Type::STR, RPCArg::Optional::NO, "The selected label, may be the default label using \"\"."},
            {"minconf", RPCArg::Type::NUM, /* default */ "1", "Only include transactions confirmed at least this many times."},
            {"addlocked", RPCArg::Type::BOOL, /* default */ "false", "Whether to include transactions locked via InstantSend."},
        },
        RPCResult{
            RPCResult::Type::STR_AMOUNT, "amount", "The total amount in " + CURRENCY_UNIT + " received for this label."
        },
        RPCExamples{
    "\nAmount received by the default label with at least 1 confirmation\n"
    + HelpExampleCli("getreceivedbylabel", "\"\"") +
    "\nAmount received at the tabby label including unconfirmed amounts with zero confirmations\n"
    + HelpExampleCli("getreceivedbylabel", "\"tabby\" 0") +
    "\nThe amount with at least 6 confirmations\n"
    + HelpExampleCli("getreceivedbylabel", "\"tabby\" 6") +
    "\nAs a JSON-RPC call\n"
    + HelpExampleRpc("getreceivedbylabel", "\"tabby\", 6")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);

    // Minimum confirmations
    int nMinDepth = 1;
    if (!request.params[1].isNull())
        nMinDepth = request.params[1].get_int();
    bool fAddLocked = (!request.params[2].isNull() && request.params[2].get_bool());

    // Get the set of pub keys assigned to label
    std::string label = LabelFromValue(request.params[0]);
    std::set<CTxDestination> setAddress = pwallet->GetLabelAddresses(label);

    // Tally
    CAmount nAmount = 0;
    for (const std::pair<const uint256, CWalletTx>& pairWtx : pwallet->mapWallet) {
        const CWalletTx& wtx = pairWtx.second;
        if (wtx.IsCoinBase() || !pwallet->chain().checkFinalTx(*wtx.tx))
            continue;

        for (const CTxOut& txout : wtx.tx->vout)
        {
            CTxDestination address;
            if (ExtractDestination(txout.scriptPubKey, address) && pwallet->IsMine(address) && setAddress.count(address)) {
                if ((wtx.GetDepthInMainChain() >= nMinDepth) || (fAddLocked && wtx.IsLockedByInstantSend()))
                    nAmount += txout.nValue;
            }
        }
    }

    return ValueFromAmount(nAmount);
}


static UniValue getbalance(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    RPCHelpMan{"getbalance",
        "\nReturns the total available balance.\n"
        "The available balance is what the wallet considers currently spendable, and is\n"
        "thus affected by options which limit spendability such as -spendzeroconfchange.\n",
        {
            {"dummy", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, "Remains for backward compatibility. Must be excluded or set to \"*\"."},
            {"minconf", RPCArg::Type::NUM, /* default */ "0", "Only include transactions confirmed at least this many times."},
            {"addlocked", RPCArg::Type::BOOL, /* default */ "false", "Whether to include transactions locked via InstantSend in the wallet's balance."},
            {"include_watchonly", RPCArg::Type::BOOL, /* default */ "true for watch-only wallets, otherwise false", "Also include balance in watch-only addresses (see 'importaddress')"},
            {"avoid_reuse", RPCArg::Type::BOOL, /* default */ "true", "(only available if avoid_reuse wallet flag is set) Do not include balance in dirty outputs; addresses are considered dirty if they have previously been used in a transaction."},
        },
        RPCResult{
            RPCResult::Type::STR_AMOUNT, "amount", "The total amount in " + CURRENCY_UNIT + " received for this wallet."
        },
        RPCExamples{
    "\nThe total amount in the wallet with 0 or more confirmations\n"
    + HelpExampleCli("getbalance", "") +
    "\nThe total amount in the wallet with at least 6 confirmations\n"
    + HelpExampleCli("getbalance", "\"*\" 6") +
    "\nAs a JSON-RPC call\n"
    + HelpExampleRpc("getbalance", "\"*\", 6")
        },
    }.Check(request);

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);

    const UniValue& dummy_value = request.params[0];
    if (!dummy_value.isNull() && dummy_value.get_str() != "*") {
        throw JSONRPCError(RPC_METHOD_DEPRECATED, "dummy first argument must be excluded or set to \"*\".");
    }

    int min_depth = 0;
    if (!request.params[1].isNull()) {
        min_depth = request.params[1].get_int();
    }

    const UniValue& addlocked = request.params[2];
    bool fAddLocked = false;
    if (!addlocked.isNull()) {
        fAddLocked = addlocked.get_bool();
    }

    bool include_watchonly = ParseIncludeWatchonly(request.params[3], *pwallet);

    bool avoid_reuse = GetAvoidReuseFlag(pwallet, request.params[4]);
    const auto bal = pwallet->GetBalance(min_depth, avoid_reuse, fAddLocked);

    return ValueFromAmount(bal.m_mine_trusted + (include_watchonly ? bal.m_watchonly_trusted : 0));
}

static UniValue getunconfirmedbalance(const JSONRPCRequest &request)
{
    RPCHelpMan{"getunconfirmedbalance",
        "DEPRECATED\nIdentical to getbalances().mine.untrusted_pending\n",
        {},
        RPCResult{RPCResult::Type::NUM, "", "The balance"},
        RPCExamples{""},
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);

    return ValueFromAmount(pwallet->GetBalance().m_mine_untrusted_pending);
}


static UniValue sendmany(const JSONRPCRequest& request)
{
    RPCHelpMan{"sendmany",
                "\nSend multiple times. Amounts are double-precision floating point numbers." +
                        HELP_REQUIRING_PASSPHRASE,
                {
                    {"dummy", RPCArg::Type::STR, RPCArg::Optional::NO, "Must be set to \"\" for backwards compatibility.", "\"\""},
                    {"amounts", RPCArg::Type::OBJ, RPCArg::Optional::NO, "The addresses and amounts",
                        {
                            {"address", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "The blocx address is the key, the numeric amount (can be string) in " + CURRENCY_UNIT + " is the value"},
                        },
                    },
                    {"minconf", RPCArg::Type::NUM, RPCArg::Optional::OMITTED_NAMED_ARG, "Ignored dummy value"},
                    {"addlocked", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED_NAMED_ARG, "Ignored dummy value"},
                    {"comment", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, "A comment"},
                    {"subtractfeefrom", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG, "The addresses.\n"
            "                           The fee will be equally deducted from the amount of each selected address.\n"
            "                           Those recipients will receive less blocx than you enter in their corresponding amount field.\n"
            "                           If no addresses are specified here, the sender pays the fee.",
                        {
                            {"address", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Subtract fee from this address"},
                        },
                    },
                    {"use_is", RPCArg::Type::BOOL, /* default */ "false", "Deprecated and ignored"},
                    {"use_cj", RPCArg::Type::BOOL, /* default */ "false", "Use CoinJoin funds only"},
                    {"conf_target", RPCArg::Type::NUM, /* default */ "wallet default", "Confirmation target (in blocks)"},
                    {"estimate_mode", RPCArg::Type::STR, /* default */ "UNSET", "The fee estimate mode, must be one of:\n"
            "       \"UNSET\"\n"
            "       \"ECONOMICAL\"\n"
            "       \"CONSERVATIVE\""},
                },
                 RPCResult{
                     RPCResult::Type::STR_HEX, "txid", "The transaction id for the send. Only 1 transaction is created regardless of\n"
            "the number of addresses."
                 },
                RPCExamples{
            "\nSend two amounts to two different addresses:\n"
            + HelpExampleCli("sendmany", "\"\" \"{\\\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwG\\\":0.01,\\\"XuQQkwA4FYkq2XERzMY2CiAZhJTEDAbtcG\\\":0.02}\"") +
            "\nSend two amounts to two different addresses setting the confirmation and comment:\n"
            + HelpExampleCli("sendmany", "\"\" \"{\\\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwG\\\":0.01,\\\"XuQQkwA4FYkq2XERzMY2CiAZhJTEDAbtcG\\\":0.02}\" 6 false \"testing\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("sendmany", "\"\", \"{\\\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwG\\\":0.01,\\\"XuQQkwA4FYkq2XERzMY2CiAZhJTEDAbtcG\\\":0.02}\", 6, false, \"testing\"")
                },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);

    if (pwallet->GetBroadcastTransactions() && !pwallet->chain().p2pEnabled()) {
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");
    }

    if (!request.params[0].isNull() && !request.params[0].get_str().empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Dummy value must be set to \"\"");
    }
    UniValue sendTo = request.params[1].get_obj();
    mapValue_t mapValue;
    if (!request.params[4].isNull() && !request.params[4].get_str().empty())
        mapValue["comment"] = request.params[4].get_str();

    UniValue subtractFeeFrom(UniValue::VARR);
    if (!request.params[5].isNull())
        subtractFeeFrom = request.params[5].get_array();

    // request.params[6] ("use_is") is deprecated and not used here

    CCoinControl coin_control;

    if (!request.params[7].isNull()) {
        coin_control.UseCoinJoin(request.params[7].get_bool());
    }

    if (!request.params[8].isNull()) {
        coin_control.m_confirm_target = ParseConfirmTarget(request.params[8], pwallet->chain().estimateMaxBlocks());
    }

    if (!request.params[9].isNull()) {
        if (!FeeModeFromString(request.params[9].get_str(), coin_control.m_fee_mode)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid estimate_mode parameter");
        }
    }

    if (coin_control.IsUsingCoinJoin()) {
        mapValue["DS"] = "1";
    }

    std::set<CTxDestination> destinations;
    std::vector<CRecipient> vecSend;

    std::vector<std::string> keys = sendTo.getKeys();
    for (const std::string& name_ : keys) {
        CTxDestination dest = DecodeDestination(name_);
        if (!IsValidDestination(dest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid BLOCX address: ") + name_);
        }

        if (destinations.count(dest)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ") + name_);
        }
        destinations.insert(dest);

        CScript scriptPubKey = GetScriptForDestination(dest);
        CAmount nAmount = AmountFromValue(sendTo[name_]);
        if (nAmount <= 0)
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");

        bool fSubtractFeeFromAmount = false;
        for (unsigned int idx = 0; idx < subtractFeeFrom.size(); idx++) {
            const UniValue& addr = subtractFeeFrom[idx];
            if (addr.get_str() == name_)
                fSubtractFeeFromAmount = true;
        }

        CRecipient recipient = {scriptPubKey, nAmount, fSubtractFeeFromAmount};
        vecSend.push_back(recipient);
    }

    EnsureWalletIsUnlocked(pwallet);

    // Send
    CAmount nFeeRequired = 0;
    int nChangePosRet = -1;
    bilingual_str error;
    CTransactionRef tx;
    bool fCreated = pwallet->CreateTransaction(vecSend, tx, nFeeRequired, nChangePosRet, error, coin_control);
    if (!fCreated)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, error.original);
    pwallet->CommitTransaction(tx, std::move(mapValue), {} /* orderForm */);
    return tx->GetHash().GetHex();
}

UniValue addmultisigaddress(const JSONRPCRequest& request)
{
    RPCHelpMan{"addmultisigaddress",
        "\nAdd a nrequired-to-sign multisignature address to the wallet. Requires a new wallet backup.\n"
        "Each key is a BLOCX address or hex-encoded public key.\n"
        "This functionality is only intended for use with non-watchonly addresses.\n"
        "See `importaddress` for watchonly p2sh address support.\n"
        "If 'label' is specified, assign address to that label.\n",
        {
            {"nrequired", RPCArg::Type::NUM, RPCArg::Optional::NO, "The number of required signatures out of the n keys or addresses."},
            {"keys", RPCArg::Type::ARR, RPCArg::Optional::NO, "The blocx addresses or hex-encoded public keys",
                {
                    {"key", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "blocx address or hex-encoded public key"},
                },
                },
            {"label", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, "A label to assign the addresses to."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "address", "The value of the new multisig address"},
                {RPCResult::Type::STR_HEX, "redeemScript", "The string value of the hex-encoded redemption script"},
            }
        },
        RPCExamples{
    "\nAdd a multisig address from 2 addresses\n"
    + HelpExampleCli("addmultisigaddress", "2 \"[\\\"Xt4qk9uKvQYAonVGSZNXqxeDmtjaEWgfrS\\\",\\\"XoSoWQkpgLpppPoyyzbUFh1fq2RBvW6UK2\\\"]\"") +
    "\nAs a JSON-RPC call\n"
    + HelpExampleRpc("addmultisigaddress", "2, \"[\\\"Xt4qk9uKvQYAonVGSZNXqxeDmtjaEWgfrS\\\",\\\"XoSoWQkpgLpppPoyyzbUFh1fq2RBvW6UK2\\\"]\"")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    LegacyScriptPubKeyMan& spk_man = EnsureLegacyScriptPubKeyMan(*pwallet);

    LOCK(pwallet->cs_wallet);

    std::string label;
    if (!request.params[2].isNull())
        label = LabelFromValue(request.params[2]);

    int required = request.params[0].get_int();

    // Get the public keys
    const UniValue& keys_or_addrs = request.params[1].get_array();
    std::vector<CPubKey> pubkeys;
    for (unsigned int i = 0; i < keys_or_addrs.size(); ++i) {
        if (IsHex(keys_or_addrs[i].get_str()) && (keys_or_addrs[i].get_str().length() == 66 || keys_or_addrs[i].get_str().length() == 130)) {
            pubkeys.push_back(HexToPubKey(keys_or_addrs[i].get_str()));
        } else {
            pubkeys.push_back(AddrToPubKey(spk_man, keys_or_addrs[i].get_str()));
        }
    }

    // Construct using pay-to-script-hash:
    CScript inner = CreateMultisigRedeemscript(required, pubkeys);
    ScriptHash innerHash(inner);
    spk_man.AddCScript(inner);

    pwallet->SetAddressBook(innerHash, label, "send");

    UniValue result(UniValue::VOBJ);
    result.pushKV("address", EncodeDestination(innerHash));
    result.pushKV("redeemScript", HexStr(inner));
    return result;
}


struct tallyitem
{
    CAmount nAmount{0};
    int nConf{std::numeric_limits<int>::max()};
    std::vector<uint256> txids;
    bool fIsWatchonly{false};
    tallyitem()
    {
    }
};

static UniValue ListReceived(CWallet * const pwallet, const UniValue& params, bool by_label) EXCLUSIVE_LOCKS_REQUIRED(pwallet->cs_wallet)
{
    // Minimum confirmations
    int nMinDepth = 1;
    if (!params[0].isNull())
        nMinDepth = params[0].get_int();
    bool fAddLocked = false;
    if (!params[1].isNull())
        fAddLocked = params[1].get_bool();

    // Whether to include empty labels
    bool fIncludeEmpty = false;
    if (!params[2].isNull())
        fIncludeEmpty = params[2].get_bool();

    isminefilter filter = ISMINE_SPENDABLE;
    if (ParseIncludeWatchonly(params[3], *pwallet)) {
        filter |= ISMINE_WATCH_ONLY;
    }

    bool has_filtered_address = false;
    CTxDestination filtered_address = CNoDestination();
    if (!by_label && params.size() > 4) {
        if (!IsValidDestinationString(params[4].get_str())) {
            throw JSONRPCError(RPC_WALLET_ERROR, "address_filter parameter was invalid");
        }
        filtered_address = DecodeDestination(params[4].get_str());
        has_filtered_address = true;
    }

    // Tally
    std::map<CTxDestination, tallyitem> mapTally;
    for (const std::pair<const uint256, CWalletTx>& pairWtx : pwallet->mapWallet) {
        const CWalletTx& wtx = pairWtx.second;

        if (wtx.IsCoinBase() || !pwallet->chain().checkFinalTx(*wtx.tx))
            continue;

        int nDepth = wtx.GetDepthInMainChain();
        if ((nDepth < nMinDepth) && !(fAddLocked && wtx.IsLockedByInstantSend()))
            continue;

        for (const CTxOut& txout : wtx.tx->vout)
        {
            CTxDestination address;
            if (!ExtractDestination(txout.scriptPubKey, address))
                continue;

            if (has_filtered_address && !(filtered_address == address)) {
                continue;
            }

            isminefilter mine = pwallet->IsMine(address);
            if(!(mine & filter))
                continue;

            tallyitem& item = mapTally[address];
            item.nAmount += txout.nValue;
            item.nConf = std::min(item.nConf, nDepth);
            item.txids.push_back(wtx.GetHash());
            if (mine & ISMINE_WATCH_ONLY)
                item.fIsWatchonly = true;
        }
    }

    // Reply
    UniValue ret(UniValue::VARR);
    std::map<std::string, tallyitem> label_tally;

    // Create mapAddressBook iterator
    // If we aren't filtering, go from begin() to end()
    auto start = pwallet->mapAddressBook.begin();
    auto end = pwallet->mapAddressBook.end();
    // If we are filtering, find() the applicable entry
    if (has_filtered_address) {
        start = pwallet->mapAddressBook.find(filtered_address);
        if (start != end) {
            end = std::next(start);
        }
    }

    for (auto item_it = start; item_it != end; ++item_it)
    {
        const CTxDestination& address = item_it->first;
        const std::string& label = item_it->second.name;
        auto it = mapTally.find(address);
        if (it == mapTally.end() && !fIncludeEmpty)
            continue;

        isminefilter mine = pwallet->IsMine(address);
        if(!(mine & filter))
            continue;

        CAmount nAmount = 0;
        int nConf = std::numeric_limits<int>::max();
        bool fIsWatchonly = false;
        if (it != mapTally.end())
        {
            nAmount = (*it).second.nAmount;
            nConf = (*it).second.nConf;
            fIsWatchonly = (*it).second.fIsWatchonly;
        }

        if (by_label)
        {
            tallyitem& _item = label_tally[label];
            _item.nAmount += nAmount;
            _item.nConf = std::min(_item.nConf, nConf);
            _item.fIsWatchonly = fIsWatchonly;
        }
        else
        {
            UniValue obj(UniValue::VOBJ);
            if(fIsWatchonly)
                obj.pushKV("involvesWatchonly", true);
            obj.pushKV("address",       EncodeDestination(address));
            obj.pushKV("amount",        ValueFromAmount(nAmount));
            obj.pushKV("confirmations", (nConf == std::numeric_limits<int>::max() ? 0 : nConf));
            obj.pushKV("label", label);
            UniValue transactions(UniValue::VARR);
            if (it != mapTally.end())
            {
                for (const uint256& _item : (*it).second.txids)
                {
                    transactions.push_back(_item.GetHex());
                }
            }
            obj.pushKV("txids", transactions);
            ret.push_back(obj);
        }
    }

    if (by_label)
    {
        for (const auto& entry : label_tally)
        {
            CAmount nAmount = entry.second.nAmount;
            int nConf = entry.second.nConf;
            UniValue obj(UniValue::VOBJ);
            if (entry.second.fIsWatchonly)
                obj.pushKV("involvesWatchonly", true);
            obj.pushKV("amount",        ValueFromAmount(nAmount));
            obj.pushKV("confirmations", (nConf == std::numeric_limits<int>::max() ? 0 : nConf));
            obj.pushKV("label",         entry.first);
            ret.push_back(obj);
        }
    }

    return ret;
}

static UniValue listreceivedbyaddress(const JSONRPCRequest& request)
{
    RPCHelpMan{"listreceivedbyaddress",
        "\nList balances by receiving address.\n",
        {
            {"minconf", RPCArg::Type::NUM, /* default */ "1", "The minimum number of confirmations before payments are included."},
            {"addlocked", RPCArg::Type::BOOL, /* default */ "false", "Whether to include transactions locked via InstantSend."},
            {"include_empty", RPCArg::Type::BOOL, /* default */ "false", "Whether to include addresses that haven't received any payments."},
            {"include_watchonly", RPCArg::Type::BOOL, /* default */ "true for watch-only wallets, otherwise false", "Whether to include watch-only addresses (see 'importaddress')"},
            {"address_filter", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, "If present, only return information on this address."},
        },
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {
                {RPCResult::Type::OBJ, "", "",
                 {
                     {RPCResult::Type::BOOL, "involvesWatchonly", "Only returns true if imported addresses were involved in transaction"},
                     {RPCResult::Type::STR, "address", "The receiving address"},
                     {RPCResult::Type::STR_AMOUNT, "amount", "The total amount in " + CURRENCY_UNIT + " received by the address"},
                     {RPCResult::Type::NUM, "confirmations", "The number of confirmations of the most recent transaction included.\n"
                                                             "If 'addlocked' is true, the number of confirmations can be less than\n"
                                                             "configured for transactions locked via InstantSend"},
                     {RPCResult::Type::STR, "label", "The label of the receiving address. The default label is \"\""},
                     {RPCResult::Type::ARR, "txids", "",
                      {
                          {RPCResult::Type::STR_HEX, "txid", "The ids of transactions received with the address"},
                      }},
                 }},
            }
        },
        RPCExamples{
            HelpExampleCli("listreceivedbyaddress", "")
    + HelpExampleCli("listreceivedbyaddress", "6 false true")
    + HelpExampleRpc("listreceivedbyaddress", "6, false, true, true")
    + HelpExampleRpc("listreceivedbyaddress", "6, false, true, true, \"XbtdLrTsrPDhGy1wXtwKYoBpuKovE3JeBK\"")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);

    return ListReceived(pwallet, request.params, false);
}

static UniValue listreceivedbylabel(const JSONRPCRequest& request)
{
    RPCHelpMan{"listreceivedbylabel",
        "\nList received transactions by label.\n",
        {
            {"minconf", RPCArg::Type::NUM, /* default */ "1", "The minimum number of confirmations before payments are included."},
            {"addlocked", RPCArg::Type::BOOL, /* default */ "false", "Whether to include transactions locked via InstantSend."},
            {"include_empty", RPCArg::Type::BOOL, /* default */ "false", "Whether to include labels that haven't received any payments."},
            {"include_watchonly", RPCArg::Type::BOOL, /* default */ "true for watch-only wallets, otherwise false", "Whether to include watch-only addresses (see 'importaddress')"},
        },
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {
                {RPCResult::Type::OBJ, "", "",
                 {
                     {RPCResult::Type::BOOL, "involvesWatchonly", "Only returns true if imported addresses were involved in transaction"},
                     {RPCResult::Type::STR_AMOUNT, "amount", "The total amount received by addresses with this label"},
                     {RPCResult::Type::NUM, "confirmations", "The number of confirmations of the most recent transaction included"},
                     {RPCResult::Type::STR, "label", "The label of the receiving address. The default label is \"\""},
                 }},
            }
        },
        RPCExamples{
            HelpExampleCli("listreceivedbylabel", "")
    + HelpExampleCli("listreceivedbylabel", "6 true")
    + HelpExampleRpc("listreceivedbylabel", "6, true, true")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);

    return ListReceived(pwallet, request.params, true);
}

static void MaybePushAddress(UniValue & entry, const CTxDestination &dest)
{
    if (IsValidDestination(dest)) {
        entry.pushKV("address", EncodeDestination(dest));
    }
}

/**
 * List transactions based on the given criteria.
 *
 * @param  pwallet        The wallet.
 * @param  wtx            The wallet transaction.
 * @param  nMinDepth      The minimum confirmation depth.
 * @param  fLong          Whether to include the JSON version of the transaction.
 * @param  ret            The UniValue into which the result is stored.
 * @param  filter_ismine  The "is mine" filter flags.
 * @param  filter_label   Optional label string to filter incoming transactions.
 */
static void ListTransactions(CWallet* const pwallet, const CWalletTx& wtx, int nMinDepth, bool fLong, UniValue& ret, const isminefilter& filter_ismine, const std::string* filter_label) EXCLUSIVE_LOCKS_REQUIRED(pwallet->cs_wallet)
{
    CAmount nFee;
    std::list<COutputEntry> listReceived;
    std::list<COutputEntry> listSent;

    wtx.GetAmounts(listReceived, listSent, nFee, filter_ismine);

    bool involvesWatchonly = wtx.IsFromMe(ISMINE_WATCH_ONLY);

    // Sent
    if (!filter_label)
    {
        for (const COutputEntry& s : listSent)
        {
            UniValue entry(UniValue::VOBJ);
            if (involvesWatchonly || (pwallet->IsMine(s.destination) & ISMINE_WATCH_ONLY)) {
                entry.pushKV("involvesWatchonly", true);
            }
            MaybePushAddress(entry, s.destination);
            std::map<std::string, std::string>::const_iterator it = wtx.mapValue.find("DS");
            entry.pushKV("category", (it != wtx.mapValue.end() && it->second == "1") ? "coinjoin" : "send");
            entry.pushKV("amount", ValueFromAmount(-s.amount));
            if (pwallet->mapAddressBook.count(s.destination)) {
                entry.pushKV("label", pwallet->mapAddressBook[s.destination].name);
            }
            entry.pushKV("vout", s.vout);
            entry.pushKV("fee", ValueFromAmount(-nFee));
            if (fLong)
                WalletTxToJSON(pwallet->chain(), wtx, entry);
            entry.pushKV("abandoned", wtx.isAbandoned());
            ret.push_back(entry);
        }
    }

    // Received
    if (listReceived.size() > 0 && ((wtx.GetDepthInMainChain() >= nMinDepth) || wtx.IsLockedByInstantSend()))
    {
        for (const COutputEntry& r : listReceived)
        {
            std::string label;
            if (pwallet->mapAddressBook.count(r.destination)) {
                label = pwallet->mapAddressBook[r.destination].name;
            }
            if (filter_label && label != *filter_label) {
                continue;
            }
            UniValue entry(UniValue::VOBJ);
            if (involvesWatchonly || (pwallet->IsMine(r.destination) & ISMINE_WATCH_ONLY)) {
                entry.pushKV("involvesWatchonly", true);
            }
            MaybePushAddress(entry, r.destination);
            if (wtx.IsCoinBase())
            {
                if (wtx.GetDepthInMainChain() < 1)
                    entry.pushKV("category", "orphan");
                else if (wtx.IsImmatureCoinBase())
                    entry.pushKV("category", "immature");
                else
                    entry.pushKV("category", "generate");
            }
            else
            {
                entry.pushKV("category", "receive");
            }
            entry.pushKV("amount", ValueFromAmount(r.amount));
            if (pwallet->mapAddressBook.count(r.destination)) {
                entry.pushKV("label", label);
            }
            entry.pushKV("vout", r.vout);
            if (fLong)
                WalletTxToJSON(pwallet->chain(), wtx, entry);
            ret.push_back(entry);
        }
    }
}

static const std::vector<RPCResult> TransactionDescriptionString()
{
    return  {{RPCResult::Type::NUM, "confirmations", "The number of blockchain confirmations for the transaction. Available for 'send' and\n"
                                                    "'receive' category of transactions. Negative confirmations indicate the\n"
                                                    "transaction conflicts with the block chain"},
            {RPCResult::Type::BOOL, "instantlock", "Current transaction lock state. Available for 'send' and 'receive' category of transactions"},
            {RPCResult::Type::BOOL, "instantlock-internal", "Current internal transaction lock state. Available for 'send' and 'receive' category of transactions"},
            {RPCResult::Type::BOOL, "chainlock", "The state of the corresponding block chainlock"},
            {RPCResult::Type::BOOL, "trusted", "Whether we consider the outputs of this unconfirmed transaction safe to spend."},
            {RPCResult::Type::STR_HEX, "blockhash", "The block hash containing the transaction. Available for 'send' and 'receive'\n"
                                                   "category of transactions."},
            {RPCResult::Type::NUM, "blockindex", "The index of the transaction in the block that includes it. Available for 'send' and 'receive'\n"
                                                "category of transactions."},
            {RPCResult::Type::NUM_TIME, "blocktime", "The block time expressed in " + UNIX_EPOCH_TIME + "."},
            {RPCResult::Type::STR_HEX, "txid", "The transaction id. Available for 'send' and 'receive' category of transactions."},
            {RPCResult::Type::NUM_TIME, "time", "The transaction time expressed in " + UNIX_EPOCH_TIME + "."},
            {RPCResult::Type::NUM_TIME, "timereceived", "The time received expressed in " + UNIX_EPOCH_TIME + ". Available \n"
                                               "for 'send' and 'receive' category of transactions."},
            {RPCResult::Type::STR, "comment", "If a comment is associated with the transaction."}};
}

static UniValue listtransactions(const JSONRPCRequest& request)
{
    RPCHelpMan{"listtransactions",
        "\nIf a label name is provided, this will return only incoming transactions paying to addresses with the specified label.\n"
        "\nReturns up to 'count' most recent transactions skipping the first 'from' transactions.\n",
        {
            {"label|dummy", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, "If set, should be a valid label name to return only incoming transactions\n"
                  "with the specified label, or \"*\" to disable filtering and return all transactions."},
            {"count", RPCArg::Type::NUM, /* default */ "10", "The number of transactions to return"},
            {"skip", RPCArg::Type::NUM, /* default */ "0", "The number of transactions to skip"},
            {"include_watchonly", RPCArg::Type::BOOL, /* default */ "true for watch-only wallets, otherwise false", "Include transactions to watch-only addresses (see 'importaddress')"},
        },
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {
                {RPCResult::Type::OBJ, "", "", Cat(Cat<std::vector<RPCResult>>(
                    {
                        {RPCResult::Type::BOOL, "involvesWatchonly", "Only returns true if imported addresses were involved in transaction"},
                        {RPCResult::Type::STR, "address", "The blocx address of the transaction. Not present for\n"
                              "move transactions (category = move)."},
                        {RPCResult::Type::STR, "category", "The transaction category.\n"
                            "\"send\"                  Transactions sent.\n"
                            "\"coinjoin\"              Transactions sent using CoinJoin funds.\n"
                            "\"receive\"               Non-coinbase transactions received.\n"
                            "\"generate\"              Coinbase transactions received with more than 100 confirmations.\n"
                            "\"immature\"              Coinbase transactions received with 100 or fewer confirmations.\n"
                            "\"orphan\"                Orphaned coinbase transactions received.\n"},
                        {RPCResult::Type::STR_AMOUNT, "amount", "The amount in " + CURRENCY_UNIT + ". This is negative for the 'send' category, and is positive\n"
                             "for all other categories"},
                        {RPCResult::Type::STR, "label", "A comment for the address/transaction, if any"},
                        {RPCResult::Type::NUM, "vout", "the vout value"},
                        {RPCResult::Type::STR_AMOUNT, "fee", "The amount of the fee in " + CURRENCY_UNIT + ". This is negative and only available for the\n"
                            "'send' category of transactions."},
                    },
                    TransactionDescriptionString()),
                    {
                       {RPCResult::Type::BOOL, "abandoned", "'true' if the transaction has been abandoned (inputs are respendable). Only available for the \n"
                                                            "'send' category of transactions."},
                    })},
            }
        },
        RPCExamples{
    "\nList the most recent 10 transactions in the systems\n"
    + HelpExampleCli("listtransactions", "") +
    "\nList transactions 100 to 120\n"
    + HelpExampleCli("listtransactions", "\"\" 20 100") +
    "\nAs a json rpc call\n"
    + HelpExampleRpc("listtransactions", "\"\", 20, 100")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    const std::string* filter_label = nullptr;
    if (!request.params[0].isNull() && request.params[0].get_str() != "*") {
        filter_label = &request.params[0].get_str();
        if (filter_label->empty()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Label argument must be a valid label name or \"*\".");
        }
    }
    int nCount = 10;
    if (!request.params[1].isNull())
        nCount = request.params[1].get_int();
    int nFrom = 0;
    if (!request.params[2].isNull())
        nFrom = request.params[2].get_int();
    isminefilter filter = ISMINE_SPENDABLE;

    if (ParseIncludeWatchonly(request.params[3], *pwallet)) {
        filter |= ISMINE_WATCH_ONLY;
    }

    if (nCount < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative count");
    if (nFrom < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative from");

    UniValue ret(UniValue::VARR);

    {
        LOCK(pwallet->cs_wallet);

        const CWallet::TxItems & txOrdered = pwallet->wtxOrdered;

        // iterate backwards until we have nCount items to return:
        for (CWallet::TxItems::const_reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it)
        {
            CWalletTx *const pwtx = (*it).second;
            ListTransactions(pwallet, *pwtx, 0, true, ret, filter, filter_label);
            if ((int)ret.size() >= (nCount+nFrom)) break;
        }
    }

    // ret is newest to oldest

    if (nFrom > (int)ret.size())
        nFrom = ret.size();
    if ((nFrom + nCount) > (int)ret.size())
        nCount = ret.size() - nFrom;

    const std::vector<UniValue>& txs = ret.getValues();
    UniValue result{UniValue::VARR};
    result.push_backV({ txs.rend() - nFrom - nCount, txs.rend() - nFrom }); // Return oldest to newest
    return result;
}

static UniValue listsinceblock(const JSONRPCRequest& request)
{
    RPCHelpMan{"listsinceblock",
        "\nGet all transactions in blocks since block [blockhash], or all transactions if omitted.\n"
        "If \"blockhash\" is no longer a part of the main chain, transactions from the fork point onward are included.\n"
        "Additionally, if include_removed is set, transactions affecting the wallet which were removed are returned in the \"removed\" array.\n",
        {
            {"blockhash", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, "If set, the block hash to list transactions since, otherwise list all transactions."},
            {"target_confirmations", RPCArg::Type::NUM, /* default */ "1", "Return the nth block hash from the main chain. e.g. 1 would mean the best block hash. Note: this is not used as a filter, but only affects [lastblock] in the return value"},
            {"include_watchonly", RPCArg::Type::BOOL, /* default */ "true for watch-only wallets, otherwise false", "Include transactions to watch-only addresses (see 'importaddress')"},
            {"include_removed", RPCArg::Type::BOOL, /* default */ "true", "Show transactions that were removed due to a reorg in the \"removed\" array\n"
    "                                                           (not guaranteed to work on pruned nodes)"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::ARR, "transactions", "",
                {
                    {RPCResult::Type::OBJ, "", "", Cat(Cat<std::vector<RPCResult>>(
                    {
                        {RPCResult::Type::BOOL, "involvesWatchonly", "Only returns true if imported addresses were involved in transaction"},
                        {RPCResult::Type::STR, "address", "The blocx address of the transaction."},
                        {RPCResult::Type::STR, "category", "The transaction category.\n"
                            "\"send\"                  Transactions sent.\n"
                            "\"coinjoin\"              Transactions sent using CoinJoin funds.\n"
                            "\"receive\"               Non-coinbase transactions received.\n"
                            "\"generate\"              Coinbase transactions received with more than 100 confirmations.\n"
                            "\"immature\"              Coinbase transactions received with 100 or fewer confirmations.\n"
                            "\"orphan\"                Orphaned coinbase transactions received.\n"},
                        {RPCResult::Type::STR_AMOUNT, "amount", "The amount in " + CURRENCY_UNIT + ". This is negative for the 'send' category, and is positive\n"
                            "for all other categories"},
                        {RPCResult::Type::NUM, "vout", "the vout value"},
                        {RPCResult::Type::NUM, "fee", "The amount of the fee in " + CURRENCY_UNIT + ". This is negative and only available for the 'send' category of transactions."},
                    },
                    TransactionDescriptionString()),
                    {
                        {RPCResult::Type::BOOL, "abandoned", "'true' if the transaction has been abandoned (inputs are respendable). Only available for the 'send' category of transactions."},
                        {RPCResult::Type::STR, "comment", "If a comment is associated with the transaction."},
                        {RPCResult::Type::STR, "label", "A comment for the address/transaction, if any."},
                        {RPCResult::Type::STR, "to", "If a comment to is associated with the transaction."},
                    })},
                }},
                {RPCResult::Type::ARR, "removed", "<structure is the same as \"transactions\" above, only present if include_removed=true>\n"
                    "Note: transactions that were re-added in the active chain will appear as-is in this array, and may thus have a positive confirmation count."
                , {{RPCResult::Type::ELISION, "", ""},}},
                {RPCResult::Type::STR_HEX, "lastblockhash", "The hash of the block (target_confirmations-1) from the best block on the main chain. This is typically used to feed back into listsinceblock the next time you call it. So you would generally use a target_confirmations of say 6, so you will be continually re-notified of transactions until they've reached 6 confirmations plus any new ones."}
            }
        },
        RPCExamples{
            HelpExampleCli("listsinceblock", "")
    + HelpExampleCli("listsinceblock", "\"000000000000000bacf66f7497b7dc45ef753ee9a7d38571037cdb1a57f663ad\" 6")
    + HelpExampleRpc("listsinceblock", "\"000000000000000bacf66f7497b7dc45ef753ee9a7d38571037cdb1a57f663ad\", 6")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);

    std::optional<int> height;    // Height of the specified block or the common ancestor, if the block provided was in a deactivated chain.
    std::optional<int> altheight; // Height of the specified block, even if it's in a deactivated chain.
    int target_confirms = 1;
    isminefilter filter = ISMINE_SPENDABLE;

    uint256 blockId;
    if (!request.params[0].isNull() && !request.params[0].get_str().empty()) {
        blockId.SetHex(request.params[0].get_str());
        height = pwallet->chain().findFork(blockId, &altheight);

        if (!height) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }
    }

    if (!request.params[1].isNull()) {
        target_confirms = request.params[1].get_int();

        if (target_confirms < 1) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter");
        }
    }

    if (ParseIncludeWatchonly(request.params[2], *pwallet)) {
        filter |= ISMINE_WATCH_ONLY;
    }

    bool include_removed = (request.params[3].isNull() || request.params[3].get_bool());

    const std::optional<int> tip_height = pwallet->chain().getHeight();
    int depth = tip_height && height ? (1 + *tip_height - *height) : -1;

    UniValue transactions(UniValue::VARR);

    for (const std::pair<const uint256, CWalletTx>& pairWtx : pwallet->mapWallet) {
        CWalletTx tx = pairWtx.second;

        if (depth == -1 || abs(tx.GetDepthInMainChain()) < depth) {
            ListTransactions(pwallet, tx, 0, true, transactions, filter, nullptr /* filter_label */);
        }
    }

    // when a reorg'd block is requested, we also list any relevant transactions
    // in the blocks of the chain that was detached
    UniValue removed(UniValue::VARR);
    while (include_removed && altheight && *altheight > *height) {
        CBlock block;
        if (!pwallet->chain().findBlock(blockId, &block) || block.IsNull()) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");
        }
        for (const CTransactionRef& tx : block.vtx) {
            auto it = pwallet->mapWallet.find(tx->GetHash());
            if (it != pwallet->mapWallet.end()) {
                // We want all transactions regardless of confirmation count to appear here,
                // even negative confirmation ones, hence the big negative.
                ListTransactions(pwallet, it->second, -100000000, true, removed, filter, nullptr /* filter_label */);
            }
        }
        blockId = block.hashPrevBlock;
        --*altheight;
    }

    int last_height = tip_height ? *tip_height + 1 - target_confirms : -1;
    uint256 lastblock = last_height >= 0 ? pwallet->chain().getBlockHash(last_height) : uint256();

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("transactions", transactions);
    if (include_removed) ret.pushKV("removed", removed);
    ret.pushKV("lastblock", lastblock.GetHex());

    return ret;
}

static UniValue gettransaction(const JSONRPCRequest& request)
{
    RPCHelpMan{"gettransaction",
        "\nGet detailed information about in-wallet transaction <txid>\n",
        {
            {"txid", RPCArg::Type::STR, RPCArg::Optional::NO, "The transaction id"},
            {"include_watchonly", RPCArg::Type::BOOL, /* default */ "true for watch-only wallets, otherwise false", "Whether to include watch-only addresses in balance calculation and details[]"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "", Cat(Cat<std::vector<RPCResult>>(
                {
                    {RPCResult::Type::STR_AMOUNT, "amount", "The amount in " + CURRENCY_UNIT},
                    {RPCResult::Type::STR_AMOUNT, "fee", "The amount of the fee in " + CURRENCY_UNIT + ". This is negative and only available for the\n"
                                                                                                       "'send' category of transactions."},
                },
                TransactionDescriptionString()),
                {
                    {RPCResult::Type::ARR, "details", "",
                        {
                            {RPCResult::Type::OBJ, "", "",
                                {
                                    {RPCResult::Type::BOOL, "involvesWatchonly", "Only returns true if imported addresses were involved in transaction"},
                                    {RPCResult::Type::STR, "address", "The blocx address involved in the transaction."},
                                    {RPCResult::Type::STR, "category", "The transaction category.\n"
                                        "\"send\"                  Transactions sent.\n"
                                        "\"coinjoin\"              Transactions sent using CoinJoin funds.\n"
                                        "\"receive\"               Non-coinbase transactions received.\n"
                                        "\"generate\"              Coinbase transactions received with more than 100 confirmations.\n"
                                        "\"immature\"              Coinbase transactions received with 100 or fewer confirmations.\n"
                                        "\"orphan\"                Orphaned coinbase transactions received.\n"},
                                    {RPCResult::Type::STR_AMOUNT, "amount", "The amount in " + CURRENCY_UNIT},
                                    {RPCResult::Type::STR, "label", "A comment for the address/transaction, if any"},
                                    {RPCResult::Type::NUM, "vout", "the vout value"},
                                    {RPCResult::Type::STR_AMOUNT, "fee", "The amount of the fee in " + CURRENCY_UNIT + ". This is negative and only available for the \n"
                                                                                                                       "'send' category of transactions."},
                                    {RPCResult::Type::BOOL, "abandoned", "'true' if the transaction has been abandoned (inputs are respendable). Only available for the \n"
                                                                         "'send' category of transactions."},
                            }},
                    }},
                  {RPCResult::Type::STR_HEX, "hex", "Raw data for transaction"},
                }),
        },
        RPCExamples{
            HelpExampleCli("gettransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
    + HelpExampleCli("gettransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\" true")
    + HelpExampleRpc("gettransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);

    uint256 hash;
    hash.SetHex(request.params[0].get_str());

    isminefilter filter = ISMINE_SPENDABLE;

    if (ParseIncludeWatchonly(request.params[1], *pwallet)) {
        filter |= ISMINE_WATCH_ONLY;
    }

    UniValue entry(UniValue::VOBJ);
    auto it = pwallet->mapWallet.find(hash);
    if (it == pwallet->mapWallet.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id");
    }
    const CWalletTx& wtx = it->second;

    CAmount nCredit = wtx.GetCredit(filter);
    CAmount nDebit = wtx.GetDebit(filter);
    CAmount nNet = nCredit - nDebit;
    CAmount nFee = (wtx.IsFromMe(filter) ? wtx.tx->GetValueOut() - nDebit : 0);

    entry.pushKV("amount", ValueFromAmount(nNet - nFee));
    if (wtx.IsFromMe(filter))
        entry.pushKV("fee", ValueFromAmount(nFee));

    WalletTxToJSON(pwallet->chain(), wtx, entry);

    UniValue details(UniValue::VARR);
    ListTransactions(pwallet, wtx, 0, false, details, filter, nullptr /* filter_label */);
    entry.pushKV("details", details);

    std::string strHex = EncodeHexTx(*wtx.tx);
    entry.pushKV("hex", strHex);

    return entry;
}

static UniValue abandontransaction(const JSONRPCRequest& request)
{
    RPCHelpMan{"abandontransaction",
        "\nMark in-wallet transaction <txid> as abandoned\n"
        "This will mark this transaction and all its in-wallet descendants as abandoned which will allow\n"
        "for their inputs to be respent.  It can be used to replace \"stuck\" or evicted transactions.\n"
        "It only works on transactions which are not included in a block and are not currently in the mempool.\n"
        "It has no effect on transactions which are already abandoned.\n",
        {
            {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
        },
        RPCResult{RPCResult::Type::NONE, "", ""},
        RPCExamples{
            HelpExampleCli("abandontransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
    + HelpExampleRpc("abandontransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);

    uint256 hash;
    hash.SetHex(request.params[0].get_str());

    if (!pwallet->mapWallet.count(hash)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id");
    }
    if (!pwallet->AbandonTransaction(hash)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not eligible for abandonment");
    }

    return NullUniValue;
}


static UniValue backupwallet(const JSONRPCRequest& request)
{
    RPCHelpMan{"backupwallet",
        "\nSafely copies current wallet file to destination, which can be a directory or a path with filename.\n",
        {
            {"destination", RPCArg::Type::STR, RPCArg::Optional::NO, "The destination directory or file"},
        },
        RPCResult{RPCResult::Type::NONE, "", ""},
        RPCExamples{
            HelpExampleCli("backupwallet", "\"backup.dat\"")
    + HelpExampleRpc("backupwallet", "\"backup.dat\"")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);

    std::string strDest = request.params[0].get_str();
    if (!pwallet->BackupWallet(strDest)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Wallet backup failed!");
    }

    return NullUniValue;
}


static UniValue keypoolrefill(const JSONRPCRequest& request)
{
    RPCHelpMan{"keypoolrefill",
        "\nFills the keypool."+
                HELP_REQUIRING_PASSPHRASE,
        {
            {"newsize", RPCArg::Type::NUM, /* default */ ToString(DEFAULT_KEYPOOL_SIZE), "The new keypool size"},
        },
        RPCResult{RPCResult::Type::NONE, "", ""},
        RPCExamples{
            HelpExampleCli("keypoolrefill", "")
    + HelpExampleRpc("keypoolrefill", "")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    if (pwallet->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Private keys are disabled for this wallet");
    }

    LOCK(pwallet->cs_wallet);

    // 0 is interpreted by TopUpKeyPool() as the default keypool size given by -keypool
    unsigned int kpSize = 0;
    if (!request.params[0].isNull()) {
        if (request.params[0].get_int() < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected valid size.");
        kpSize = (unsigned int)request.params[0].get_int();
    }

    EnsureWalletIsUnlocked(pwallet);
    pwallet->TopUpKeyPool(kpSize);

    if (pwallet->GetKeyPoolSize() < (pwallet->IsHDEnabled() ? kpSize * 2 : kpSize)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error refreshing keypool.");
    }

    return NullUniValue;
}


static UniValue walletpassphrase(const JSONRPCRequest& request)
{
    RPCHelpMan{"walletpassphrase",
        "\nStores the wallet decryption key in memory for 'timeout' seconds.\n"
        "This is needed prior to performing transactions related to private keys such as sending blocx\n"
        "\nNote:\n"
        "Issuing the walletpassphrase command while the wallet is already unlocked will set a new unlock\n"
        "time that overrides the old one.\n",
        {
            {"passphrase", RPCArg::Type::STR, RPCArg::Optional::NO, "The wallet passphrase"},
            {"timeout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The time to keep the decryption key in seconds; capped at 100000000 (~3 years)."},
            {"mixingonly", RPCArg::Type::BOOL, /* default */ "false", "If is true sending functions are disabled."},
        },
        RPCResult{RPCResult::Type::NONE, "", ""},
        RPCExamples{
    "\nUnlock the wallet for 60 seconds\n"
    + HelpExampleCli("walletpassphrase", "\"my pass phrase\" 60") +
    "\nUnlock the wallet for 60 seconds but allow CoinJoin only\n"
    + HelpExampleCli("walletpassphrase", "\"my pass phrase\" 60 true") +
    "\nLock the wallet again (before 60 seconds)\n"
    + HelpExampleCli("walletlock", "") +
    "\nAs a JSON-RPC call\n"
    + HelpExampleRpc("walletpassphrase", "\"my pass phrase\", 60")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    LOCK(pwallet->cs_wallet);

    if (!pwallet->IsCrypted()) {
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletpassphrase was called.");
    }

    // Note that the walletpassphrase is stored in request.params[0] which is not mlock()ed
    SecureString strWalletPass;
    strWalletPass.reserve(100);
    // TODO: get rid of this .c_str() by implementing SecureString::operator=(std::string)
    // Alternately, find a way to make request.params[0] mlock()'d to begin with.
    strWalletPass = request.params[0].get_str().c_str();

    // Get the timeout
    int64_t nSleepTime = request.params[1].get_int64();
    // Timeout cannot be negative, otherwise it will relock immediately
    if (nSleepTime < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Timeout cannot be negative.");
    }
    // Clamp timeout
    constexpr int64_t MAX_SLEEP_TIME = 100000000; // larger values trigger a macos/libevent bug?
    if (nSleepTime > MAX_SLEEP_TIME) {
        nSleepTime = MAX_SLEEP_TIME;
    }

    bool fForMixingOnly = false;
    if (!request.params[2].isNull())
        fForMixingOnly = request.params[2].get_bool();

    if (fForMixingOnly && !pwallet->IsLocked()) {
        // Downgrading from "fuly unlocked" mode to "mixing only" one is not supported.
        // Updating unlock time when current unlock mode is not changed or when it is upgraded
        // from "mixing only" to "fuly unlocked" is ok.
        throw JSONRPCError(RPC_WALLET_ALREADY_UNLOCKED, "Error: Wallet is already fully unlocked.");
    }

    if (strWalletPass.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "passphrase can not be empty");
    }

    if (!pwallet->Unlock(strWalletPass, fForMixingOnly)) {
        throw JSONRPCError(RPC_WALLET_PASSPHRASE_INCORRECT, "Error: The wallet passphrase entered was incorrect.");
    }

    pwallet->TopUpKeyPool();

    pwallet->nRelockTime = GetTime() + nSleepTime;

    // Keep a weak pointer to the wallet so that it is possible to unload the
    // wallet before the following callback is called. If a valid shared pointer
    // is acquired in the callback then the wallet is still loaded.
    std::weak_ptr<CWallet> weak_wallet = wallet;
    pwallet->chain().rpcRunLater(strprintf("lockwallet(%s)", pwallet->GetName()), [weak_wallet] {
        if (auto shared_wallet = weak_wallet.lock()) {
            LOCK(shared_wallet->cs_wallet);
            shared_wallet->Lock();
            shared_wallet->nRelockTime = 0;
        }
    }, nSleepTime);

    return NullUniValue;
}


static UniValue walletpassphrasechange(const JSONRPCRequest& request)
{
    RPCHelpMan{"walletpassphrasechange",
        "\nChanges the wallet passphrase from 'oldpassphrase' to 'newpassphrase'.\n",
        {
            {"oldpassphrase", RPCArg::Type::STR, RPCArg::Optional::NO, "The current passphrase"},
            {"newpassphrase", RPCArg::Type::STR, RPCArg::Optional::NO, "The new passphrase"},
        },
        RPCResult{RPCResult::Type::NONE, "", ""},
        RPCExamples{
            HelpExampleCli("walletpassphrasechange", "\"old one\" \"new one\"")
    + HelpExampleRpc("walletpassphrasechange", "\"old one\", \"new one\"")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    LOCK(pwallet->cs_wallet);

    if (!pwallet->IsCrypted()) {
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletpassphrasechange was called.");
    }

    // TODO: get rid of these .c_str() calls by implementing SecureString::operator=(std::string)
    // Alternately, find a way to make request.params[0] mlock()'d to begin with.
    SecureString strOldWalletPass;
    strOldWalletPass.reserve(100);
    strOldWalletPass = request.params[0].get_str().c_str();

    SecureString strNewWalletPass;
    strNewWalletPass.reserve(100);
    strNewWalletPass = request.params[1].get_str().c_str();

    if (strOldWalletPass.empty() || strNewWalletPass.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "passphrase can not be empty");
    }

    if (!pwallet->ChangeWalletPassphrase(strOldWalletPass, strNewWalletPass)) {
        throw JSONRPCError(RPC_WALLET_PASSPHRASE_INCORRECT, "Error: The wallet passphrase entered was incorrect.");
    }

    return NullUniValue;
}


static UniValue walletlock(const JSONRPCRequest& request)
{
    RPCHelpMan{"walletlock",
        "\nRemoves the wallet encryption key from memory, locking the wallet.\n"
        "After calling this method, you will need to call walletpassphrase again\n"
        "before being able to call any methods which require the wallet to be unlocked.\n",
        {},
        RPCResult{RPCResult::Type::NONE, "", ""},
        RPCExamples{
    "\nSet the passphrase for 2 minutes to perform a transaction\n"
    + HelpExampleCli("walletpassphrase", "\"my pass phrase\" 120") +
    "\nPerform a send (requires passphrase set)\n"
    + HelpExampleCli("sendtoaddress", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwG\" 1.0") +
    "\nClear the passphrase since we are done before 2 minutes is up\n"
    + HelpExampleCli("walletlock", "") +
    "\nAs a JSON-RPC call\n"
    + HelpExampleRpc("walletlock", "")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    LOCK(pwallet->cs_wallet);

    if (!pwallet->IsCrypted()) {
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletlock was called.");
    }

    pwallet->Lock();
    pwallet->nRelockTime = 0;

    return NullUniValue;
}


static UniValue encryptwallet(const JSONRPCRequest& request)
{
    RPCHelpMan{"encryptwallet",
        "\nEncrypts the wallet with 'passphrase'. This is for first time encryption.\n"
        "After this, any calls that interact with private keys such as sending or signing \n"
        "will require the passphrase to be set prior the making these calls.\n"
        "Use the walletpassphrase call for this, and then walletlock call.\n"
        "If the wallet is already encrypted, use the walletpassphrasechange call.\n",
        {
            {"passphrase", RPCArg::Type::STR, RPCArg::Optional::NO, "The pass phrase to encrypt the wallet with. It must be at least 1 character, but should be long."},
        },
        RPCResult{RPCResult::Type::STR, "", "A string with further instructions"},
        RPCExamples{
    "\nEncrypt your wallet\n"
    + HelpExampleCli("encryptwallet", "\"my pass phrase\"") +
    "\nNow set the passphrase to use the wallet, such as for signing or sending blocx\n"
    + HelpExampleCli("walletpassphrase", "\"my pass phrase\"") +
    "\nNow we can do something like sign\n"
    + HelpExampleCli("signmessage", "\"address\" \"test message\"") +
    "\nNow lock the wallet again by removing the passphrase\n"
    + HelpExampleCli("walletlock", "") +
    "\nAs a JSON-RPC call\n"
    + HelpExampleRpc("encryptwallet", "\"my pass phrase\"")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    LOCK(pwallet->cs_wallet);

    if (pwallet->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Error: wallet does not contain private keys, nothing to encrypt.");
    }

    if (pwallet->IsCrypted()) {
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an encrypted wallet, but encryptwallet was called.");
    }

    // TODO: get rid of this .c_str() by implementing SecureString::operator=(std::string)
    // Alternately, find a way to make request.params[0] mlock()'d to begin with.
    SecureString strWalletPass;
    strWalletPass.reserve(100);
    strWalletPass = request.params[0].get_str().c_str();

    if (strWalletPass.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "passphrase can not be empty");
    }

    if (!pwallet->EncryptWallet(strWalletPass)) {
        throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Error: Failed to encrypt the wallet.");
    }

    return "wallet encrypted; The keypool has been flushed and a new HD seed was generated (if you are using HD). You need to make a new backup.";
}

static UniValue lockunspent(const JSONRPCRequest& request)
{
    RPCHelpMan{"lockunspent",
        "\nUpdates list of temporarily unspendable outputs.\n"
        "Temporarily lock (unlock=false) or unlock (unlock=true) specified transaction outputs.\n"
        "If no transaction outputs are specified when unlocking then all current locked transaction outputs are unlocked.\n"
        "A locked transaction output will not be chosen by automatic coin selection, when spending blocx.\n"
        "Locks are stored in memory only. Nodes start with zero locked outputs, and the locked output list\n"
        "is always cleared (by virtue of process exit) when a node stops or fails.\n"
        "Also see the listunspent call\n",
        {
            {"unlock", RPCArg::Type::BOOL, RPCArg::Optional::NO, "Whether to unlock (true) or lock (false) the specified transactions"},
            {"transactions", RPCArg::Type::ARR, /* default */ "empty array", "The transaction outputs and within each, the txid (string) vout (numeric).",
                {
                    {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                            {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                        },
                    },
                },
            },
        },
        RPCResult{
            RPCResult::Type::BOOL, "", "Whether the command was successful or not"
        },
        RPCExamples{
    "\nList the unspent transactions\n"
    + HelpExampleCli("listunspent", "") +
    "\nLock an unspent transaction\n"
    + HelpExampleCli("lockunspent", "false \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") +
    "\nList the locked transactions\n"
    + HelpExampleCli("listlockunspent", "") +
    "\nUnlock the transaction again\n"
    + HelpExampleCli("lockunspent", "true \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") +
    "\nAs a JSON-RPC call\n"
    + HelpExampleRpc("lockunspent", "false, \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);

    RPCTypeCheckArgument(request.params[0], UniValue::VBOOL);

    bool fUnlock = request.params[0].get_bool();

    if (request.params[1].isNull()) {
        if (fUnlock)
            pwallet->UnlockAllCoins();
        return true;
    }

    RPCTypeCheckArgument(request.params[1], UniValue::VARR);

    const UniValue& output_params = request.params[1];

    // Create and validate the COutPoints first.

    std::vector<COutPoint> outputs;
    outputs.reserve(output_params.size());

    for (unsigned int idx = 0; idx < output_params.size(); idx++) {
        const UniValue& o = output_params[idx].get_obj();

        RPCTypeCheckObj(o,
            {
                {"txid", UniValueType(UniValue::VSTR)},
                {"vout", UniValueType(UniValue::VNUM)},
            });

        const std::string& txid = find_value(o, "txid").get_str();
        if (!IsHex(txid)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected hex txid");
        }

        const int nOutput = find_value(o, "vout").get_int();
        if (nOutput < 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout cannot be negative");
        }

        const COutPoint outpt(uint256S(txid), nOutput);

        const auto it = pwallet->mapWallet.find(outpt.hash);
        if (it == pwallet->mapWallet.end()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, unknown transaction");
        }

        const CWalletTx& trans = it->second;

        if (outpt.n >= trans.tx->vout.size()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout index out of bounds");
        }

        if (pwallet->IsSpent(outpt.hash, outpt.n)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected unspent output");
        }

        const bool is_locked = pwallet->IsLockedCoin(outpt.hash, outpt.n);

        if (fUnlock && !is_locked) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected locked output");
        }

        if (!fUnlock && is_locked) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, output already locked");
        }

        outputs.push_back(outpt);
    }

    // Atomically set (un)locked status for the outputs.
    for (const COutPoint& outpt : outputs) {
        if (fUnlock) pwallet->UnlockCoin(outpt);
        else pwallet->LockCoin(outpt);
    }

    return true;
}

static UniValue listlockunspent(const JSONRPCRequest& request)
{
    RPCHelpMan{"listlockunspent",
        "\nReturns list of temporarily unspendable outputs.\n"
        "See the lockunspent call to lock and unlock transactions for spending.\n",
        {},
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {
                {RPCResult::Type::OBJ, "", "",
                {
                    {RPCResult::Type::STR_HEX, "txid", "The transaction id locked"},
                    {RPCResult::Type::NUM, "vout", "The vout value"},
                }},
            }
        },
        RPCExamples{
    "\nList the unspent transactions\n"
    + HelpExampleCli("listunspent", "") +
    "\nLock an unspent transaction\n"
    + HelpExampleCli("lockunspent", "false \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") +
    "\nList the locked transactions\n"
    + HelpExampleCli("listlockunspent", "") +
    "\nUnlock the transaction again\n"
    + HelpExampleCli("lockunspent", "true \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") +
    "\nAs a JSON-RPC call\n"
    + HelpExampleRpc("listlockunspent", "")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    LOCK(pwallet->cs_wallet);

    std::vector<COutPoint> vOutpts;
    pwallet->ListLockedCoins(vOutpts);

    UniValue ret(UniValue::VARR);

    for (const COutPoint& outpt : vOutpts) {
        UniValue o(UniValue::VOBJ);

        o.pushKV("txid", outpt.hash.GetHex());
        o.pushKV("vout", (int)outpt.n);
        ret.push_back(o);
    }

    return ret;
}

static UniValue settxfee(const JSONRPCRequest& request)
{
    RPCHelpMan{"settxfee",
        "\nSet the transaction fee per kB for this wallet. Overrides the global -paytxfee command line parameter.\n"
        "Can be deactivated by passing 0 as the fee. In that case automatic fee selection will be used by default.\n",
        {
            {"amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "The transaction fee in " + CURRENCY_UNIT + "/kB"},
        },
        RPCResult{
            RPCResult::Type::BOOL, "", "Returns true if successful"
        },
        RPCExamples{
            HelpExampleCli("settxfee", "0.00001")
    + HelpExampleRpc("settxfee", "0.00001")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    LOCK(pwallet->cs_wallet);

    CAmount nAmount = AmountFromValue(request.params[0]);
    CFeeRate tx_fee_rate(nAmount, 1000);
    CFeeRate max_tx_fee_rate(pwallet->m_default_max_tx_fee, 1000);
    if (tx_fee_rate == 0) {
        // automatic selection
    } else if (tx_fee_rate < pwallet->chain().relayMinFee()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("txfee cannot be less than min relay tx fee (%s)", pwallet->chain().relayMinFee().ToString()));
    } else if (tx_fee_rate < pwallet->m_min_fee) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("txfee cannot be less than wallet min fee (%s)", pwallet->m_min_fee.ToString()));
    } else if (tx_fee_rate > max_tx_fee_rate) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("txfee cannot be more than wallet max tx fee (%s)", max_tx_fee_rate.ToString()));
    }

    pwallet->m_pay_tx_fee = tx_fee_rate;
    return true;
}

static UniValue setcoinjoinrounds(const JSONRPCRequest& request)
{
    RPCHelpMan{"setcoinjoinrounds",
        "\nSet the number of rounds for CoinJoin.\n",
        {
            {"rounds", RPCArg::Type::NUM, RPCArg::Optional::NO,
                "The default number of rounds is " + ToString(DEFAULT_COINJOIN_ROUNDS) +
                " Cannot be more than " + ToString(MAX_COINJOIN_ROUNDS) + " nor less than " + ToString(MIN_COINJOIN_ROUNDS)},
        },
        RPCResults{},
        RPCExamples{
            HelpExampleCli("setcoinjoinrounds", "4")
    + HelpExampleRpc("setcoinjoinrounds", "16")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;

    int nRounds = request.params[0].get_int();

    if (nRounds > MAX_COINJOIN_ROUNDS || nRounds < MIN_COINJOIN_ROUNDS)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid number of rounds");

    CCoinJoinClientOptions::SetRounds(nRounds);

    return NullUniValue;
}

static UniValue setcoinjoinamount(const JSONRPCRequest& request)
{
    RPCHelpMan{"setcoinjoinamount",
        "\nSet the goal amount in " + CURRENCY_UNIT + " for CoinJoin.\n",
        {
            {"amount", RPCArg::Type::NUM, RPCArg::Optional::NO,
                "The default amount is " + ToString(DEFAULT_COINJOIN_AMOUNT) +
                " Cannot be more than " + ToString(MAX_COINJOIN_AMOUNT) + " nor less than " + ToString(MIN_COINJOIN_AMOUNT)},
        },
        RPCResults{},
        RPCExamples{
            HelpExampleCli("setcoinjoinamount", "500")
    + HelpExampleRpc("setcoinjoinamount", "208")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;

    int nAmount = request.params[0].get_int();

    if (nAmount > MAX_COINJOIN_AMOUNT || nAmount < MIN_COINJOIN_AMOUNT)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid amount of " + CURRENCY_UNIT + " as mixing goal amount");

    CCoinJoinClientOptions::SetAmount(nAmount);

    return NullUniValue;
}

static UniValue getbalances(const JSONRPCRequest& request)
{
    RPCHelpMan{"getbalances",
                "Returns an object with all balances in " + CURRENCY_UNIT + ".\n",
                {},
                RPCResult{
                    RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::OBJ, "mine", "balances from outputs that the wallet can sign",
                            {
                                {RPCResult::Type::STR_AMOUNT, "trusted", " trusted balance (outputs created by the wallet or confirmed outputs)"},
                                {RPCResult::Type::STR_AMOUNT, "untrusted_pending", " untrusted pending balance (outputs created by others that are in the mempool)"},
                                {RPCResult::Type::STR_AMOUNT, "immature", " balance from immature coinbase outputs"},
                                {RPCResult::Type::STR_AMOUNT, "used", "(only present if avoid_reuse is set) balance from coins sent to addresses that were previously spent from (potentially privacy violating)"},
                                {RPCResult::Type::STR_AMOUNT, "coinjoin", " CoinJoin balance (outputs with enough rounds created by the wallet via mixing)"},
                        }},
                        {RPCResult::Type::OBJ, "watchonly", "watchonly balances (not present if wallet does not watch anything)",
                            {
                                {RPCResult::Type::STR_AMOUNT, "trusted", " trusted balance (outputs created by the wallet or confirmed outputs)"},
                                {RPCResult::Type::STR_AMOUNT, "untrusted_pending", " untrusted pending balance (outputs created by others that are in the mempool)"},
                                {RPCResult::Type::STR_AMOUNT, "immature", " balance from immature coinbase outputs"},
                        }},
                    },
                },
                RPCExamples{
                    HelpExampleCli("getbalances", "") +
                    HelpExampleRpc("getbalances", "")
                },
    }.Check(request);

    std::shared_ptr<CWallet> const rpc_wallet = GetWalletForJSONRPCRequest(request);
    if (!rpc_wallet) return NullUniValue;
    CWallet& wallet = *rpc_wallet;

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    wallet.BlockUntilSyncedToCurrentChain();

    LOCK(wallet.cs_wallet);

    const auto bal = wallet.GetBalance();
    UniValue balances{UniValue::VOBJ};
    {
        UniValue balances_mine{UniValue::VOBJ};
        balances_mine.pushKV("trusted", ValueFromAmount(bal.m_mine_trusted));
        balances_mine.pushKV("untrusted_pending", ValueFromAmount(bal.m_mine_untrusted_pending));
        balances_mine.pushKV("immature", ValueFromAmount(bal.m_mine_immature));
        if (wallet.IsWalletFlagSet(WALLET_FLAG_AVOID_REUSE)) {
            // If the AVOID_REUSE flag is set, bal has been set to just the un-reused address balance. Get
            // the total balance, and then subtract bal to get the reused address balance.
            const auto full_bal = wallet.GetBalance(0, false);
            balances_mine.pushKV("used", ValueFromAmount(full_bal.m_mine_trusted + full_bal.m_mine_untrusted_pending - bal.m_mine_trusted - bal.m_mine_untrusted_pending));
        }
        balances_mine.pushKV("coinjoin", ValueFromAmount(bal.m_anonymized));
        balances.pushKV("mine", balances_mine);
    }
    auto spk_man = wallet.GetLegacyScriptPubKeyMan();
    if (spk_man && spk_man->HaveWatchOnly()) {
        UniValue balances_watchonly{UniValue::VOBJ};
        balances_watchonly.pushKV("trusted", ValueFromAmount(bal.m_watchonly_trusted));
        balances_watchonly.pushKV("untrusted_pending", ValueFromAmount(bal.m_watchonly_untrusted_pending));
        balances_watchonly.pushKV("immature", ValueFromAmount(bal.m_watchonly_immature));
        balances.pushKV("watchonly", balances_watchonly);
    }
    return balances;
}

static UniValue getwalletinfo(const JSONRPCRequest& request)
{
    RPCHelpMan{"getwalletinfo",
                "Returns an object containing various wallet state info.\n",
                {},
                RPCResult{
                        RPCResult::Type::OBJ, "", "",
                        {
                            {RPCResult::Type::STR, "walletname", "the wallet name"},
                            {RPCResult::Type::NUM, "walletversion", "the wallet version"},
                            {RPCResult::Type::NUM, "balance", "DEPRECATED. Identical to getbalances().mine.trusted"},
                            {RPCResult::Type::NUM, "coinjoin_balance", "DEPRECATED. Identical to getbalances().mine.coinjoin"},
                            {RPCResult::Type::NUM, "unconfirmed_balance", "DEPRECATED. Identical to getbalances().mine.untrusted_pending"},
                            {RPCResult::Type::NUM, "immature_balance", "DEPRECATED. Identical to getbalances().mine.immature"},
                            {RPCResult::Type::NUM, "txcount", "the total number of transactions in the wallet"},
                            {RPCResult::Type::NUM_TIME, "timefirstkey", "the " + UNIX_EPOCH_TIME + " of the oldest known key in the wallet"},
                            {RPCResult::Type::NUM_TIME, "keypoololdest", "the " + UNIX_EPOCH_TIME + " of the oldest pre-generated key in the key pool"},
                            {RPCResult::Type::NUM, "keypoolsize", "how many new keys are pre-generated (only counts external keys)"},
                            {RPCResult::Type::NUM, "keypoolsize_hd_internal", "how many new keys are pre-generated for internal use (used for change outputs, only appears if the wallet is using this feature, otherwise external keys are used)"},
                            {RPCResult::Type::NUM, "keys_left", "how many new keys are left since last automatic backup"},
                            {RPCResult::Type::NUM_TIME, "unlocked_until", "the " + UNIX_EPOCH_TIME + " until which the wallet is unlocked for transfers, or 0 if the wallet is locked"},
                            {RPCResult::Type::STR_AMOUNT, "paytxfee", "the transaction fee configuration, set in " + CURRENCY_UNIT + "/kB"},
                            {RPCResult::Type::STR_HEX, "hdchainid", "the ID of the HD chain"},
                            {RPCResult::Type::STR, "hdaccountcount", "how many accounts of the HD chain are in this wallet"},
                            {RPCResult::Type::ARR, "hdaccounts", "",
                                {
                                {RPCResult::Type::OBJ, "", "",
                                    {
                                        {RPCResult::Type::NUM, "hdaccountindex", "the index of the account"},
                                        {RPCResult::Type::NUM, "hdexternalkeyindex", "current external childkey index"},
                                        {RPCResult::Type::NUM, "hdinternalkeyindex", "current internal childkey index"},
                                }},
                            }},
                            {RPCResult::Type::BOOL, "avoid_reuse", "whether this wallet tracks clean/dirty coins in terms of reuse"},
                            {RPCResult::Type::OBJ, "scanning", "current scanning details, or false if no scan is in progress",
                                {
                                     {RPCResult::Type::NUM, "duration", "elapsed seconds since scan start"},
                                     {RPCResult::Type::NUM, "progress", "scanning progress percentage [0.0, 1.0]"},
                                }},
                            {RPCResult::Type::BOOL, "private_keys_enabled", "false if privatekeys are disabled for this wallet (enforced watch-only wallet)"},
                        },
                },
                RPCExamples{
                    HelpExampleCli("getwalletinfo", "")
            + HelpExampleRpc("getwalletinfo", "")
                },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);

    LegacyScriptPubKeyMan* spk_man = pwallet->GetLegacyScriptPubKeyMan();
    CHDChain hdChainCurrent;
    bool fHDEnabled = spk_man && spk_man->GetHDChain(hdChainCurrent);
    UniValue obj(UniValue::VOBJ);

    const auto bal = pwallet->GetBalance();
    obj.pushKV("walletname", pwallet->GetName());
    obj.pushKV("walletversion", pwallet->GetVersion());
    obj.pushKV("balance", ValueFromAmount(bal.m_mine_trusted));
    obj.pushKV("coinjoin_balance",       ValueFromAmount(bal.m_anonymized));
    obj.pushKV("unconfirmed_balance", ValueFromAmount(bal.m_mine_untrusted_pending));
    obj.pushKV("immature_balance", ValueFromAmount(bal.m_mine_immature));
    obj.pushKV("txcount",       (int)pwallet->mapWallet.size());
    if (spk_man) {
        AssertLockHeld(spk_man->cs_wallet);
        obj.pushKV("timefirstkey", spk_man->GetTimeFirstKey());
        obj.pushKV("keypoololdest", spk_man->GetOldestKeyPoolTime());
        obj.pushKV("keypoolsize",   (int64_t)spk_man->KeypoolCountExternalKeys());
        obj.pushKV("keypoolsize_hd_internal",   (int64_t)(spk_man->KeypoolCountInternalKeys()));
    }
    obj.pushKV("keys_left",     pwallet->nKeysLeftSinceAutoBackup);
    if (pwallet->IsCrypted())
        obj.pushKV("unlocked_until", pwallet->nRelockTime);
    obj.pushKV("paytxfee",      ValueFromAmount(pwallet->m_pay_tx_fee.GetFeePerK()));
    if (fHDEnabled) {
        obj.pushKV("hdchainid", hdChainCurrent.GetID().GetHex());
        obj.pushKV("hdaccountcount", (int64_t)hdChainCurrent.CountAccounts());
        UniValue accounts(UniValue::VARR);
        for (size_t i = 0; i < hdChainCurrent.CountAccounts(); ++i)
        {
            CHDAccount acc;
            UniValue account(UniValue::VOBJ);
            account.pushKV("hdaccountindex", (int64_t)i);
            if(hdChainCurrent.GetAccount(i, acc)) {
                account.pushKV("hdexternalkeyindex", (int64_t)acc.nExternalChainCounter);
                account.pushKV("hdinternalkeyindex", (int64_t)acc.nInternalChainCounter);
            } else {
                account.pushKV("error", strprintf("account %d is missing", i));
            }
            accounts.push_back(account);
        }
        obj.pushKV("hdaccounts", accounts);
    }
    obj.pushKV("avoid_reuse", pwallet->IsWalletFlagSet(WALLET_FLAG_AVOID_REUSE));
    if (pwallet->IsScanning()) {
        UniValue scanning(UniValue::VOBJ);
        scanning.pushKV("duration", pwallet->ScanningDuration() / 1000);
        scanning.pushKV("progress", pwallet->ScanningProgress());
        obj.pushKV("scanning", scanning);
    } else {
        obj.pushKV("scanning", false);
    }
    obj.pushKV("private_keys_enabled", !pwallet->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS));
    return obj;
}

static UniValue listwalletdir(const JSONRPCRequest& request)
{
    RPCHelpMan{"listwalletdir",
        "Returns a list of wallets in the wallet directory.\n",
        {},
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::ARR, "wallets", "",
                        {
                    {RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::STR, "name", "The wallet name"},
                    }},
                }},
            }
        },
        RPCExamples{
            HelpExampleCli("listwalletdir", "")
    + HelpExampleRpc("listwalletdir", "")
        },
    }.Check(request);

    UniValue wallets(UniValue::VARR);
    for (const auto& path : ListDatabases(GetWalletDir())) {
        UniValue wallet(UniValue::VOBJ);
        wallet.pushKV("name", path.string());
        wallets.push_back(wallet);
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("wallets", wallets);
    return result;
}

static UniValue listwallets(const JSONRPCRequest& request)
{
    RPCHelpMan{"listwallets",
        "Returns a list of currently loaded wallets.\n"
        "For full information on the wallet, use \"getwalletinfo\"\n",
        {},
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {
                {RPCResult::Type::STR, "walletname", "the wallet name"},
            }
        },
        RPCExamples{
            HelpExampleCli("listwallets", "")
    + HelpExampleRpc("listwallets", "")
        },
    }.Check(request);

    UniValue obj(UniValue::VARR);

    for (const std::shared_ptr<CWallet>& wallet : GetWallets()) {
        LOCK(wallet->cs_wallet);
        obj.push_back(wallet->GetName());
    }

    return obj;
}

static UniValue upgradetohd(const JSONRPCRequest& request)
{
    RPCHelpMan{"upgradetohd",
        "\nUpgrades non-HD wallets to HD.\n"
        "\nWarning: You will need to make a new backup of your wallet after setting the HD wallet mnemonic.\n",
        {
            {"mnemonic", RPCArg::Type::STR, /* default */ "", "Mnemonic as defined in BIP39 to use for the new HD wallet. Use an empty string \"\" to generate a new random mnemonic."},
            {"mnemonicpassphrase", RPCArg::Type::STR, /* default */ "", "Optional mnemonic passphrase as defined in BIP39"},
            {"walletpassphrase", RPCArg::Type::STR, /* default */ "", "If your wallet is encrypted you must have your wallet passphrase here. If your wallet is not encrypted specifying wallet passphrase will trigger wallet encryption."},
            {"rescan", RPCArg::Type::BOOL, /* default */ "false if mnemonic is empty", "Whether to rescan the blockchain for missing transactions or not"},
        },
        RPCResult{
            RPCResult::Type::BOOL, "", "true if successful"
        },
        RPCExamples{
            HelpExampleCli("upgradetohd", "")
    + HelpExampleCli("upgradetohd", "\"mnemonicword1 ... mnemonicwordN\"")
    + HelpExampleCli("upgradetohd", "\"mnemonicword1 ... mnemonicwordN\" \"mnemonicpassphrase\"")
    + HelpExampleCli("upgradetohd", "\"mnemonicword1 ... mnemonicwordN\" \"mnemonicpassphrase\" \"walletpassphrase\"")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();
    LegacyScriptPubKeyMan* spk_man = pwallet->GetLegacyScriptPubKeyMan();
    if (!spk_man) {
        throw JSONRPCError(RPC_WALLET_ERROR, "This type of wallet does not support this command");
    }

    bool generate_mnemonic = request.params[0].isNull() || request.params[0].get_str().empty();

    {
        LOCK(pwallet->cs_wallet);

        // Do not do anything to HD wallets
        if (pwallet->IsHDEnabled()) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Cannot upgrade a wallet to HD if it is already upgraded to HD.");
        }

        if (!pwallet->SetMaxVersion(FEATURE_HD)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Cannot downgrade wallet");
        }

        if (pwallet->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Error: Private keys are disabled for this wallet");
        }

        bool prev_encrypted = pwallet->IsCrypted();

        SecureString secureWalletPassphrase;
        secureWalletPassphrase.reserve(100);
        // TODO: get rid of this .c_str() by implementing SecureString::operator=(std::string)
        // Alternately, find a way to make request.params[0] mlock()'d to begin with.
        if (request.params[2].isNull()) {
            if (prev_encrypted) {
                throw JSONRPCError(RPC_WALLET_PASSPHRASE_INCORRECT, "Cannot upgrade encrypted wallet to HD without the wallet passphrase");
            }
        } else {
            secureWalletPassphrase = request.params[2].get_str().c_str();
            if (!pwallet->Unlock(secureWalletPassphrase)) {
                throw JSONRPCError(RPC_WALLET_PASSPHRASE_INCORRECT, "The wallet passphrase entered was incorrect");
            }
        }

        SecureString secureMnemonic;
        secureMnemonic.reserve(256);
        if (!generate_mnemonic) {
            secureMnemonic = request.params[0].get_str().c_str();
        }

        SecureString secureMnemonicPassphrase;
        secureMnemonicPassphrase.reserve(256);
        if (!request.params[1].isNull()) {
            secureMnemonicPassphrase = request.params[1].get_str().c_str();
        }

        pwallet->WalletLogPrintf("Upgrading wallet to HD\n");
        pwallet->SetMinVersion(FEATURE_HD);

        if (prev_encrypted) {
            if (!spk_man->GenerateNewHDChainEncrypted(secureMnemonic, secureMnemonicPassphrase, secureWalletPassphrase)) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Failed to generate encrypted HD wallet");
            }
        } else {
            spk_man->GenerateNewHDChain(secureMnemonic, secureMnemonicPassphrase);
            if (!secureWalletPassphrase.empty()) {
                if (!pwallet->EncryptWallet(secureWalletPassphrase)) {
                    throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Failed to encrypt HD wallet");
                }
            }
        }
    }

    // If you are generating new mnemonic it is assumed that the addresses have never gotten a transaction before, so you don't need to rescan for transactions
    bool rescan = request.params[3].isNull() ? !generate_mnemonic : request.params[3].get_bool();
    if (rescan) {
        WalletRescanReserver reserver(pwallet);
        if (!reserver.reserve()) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is currently rescanning. Abort existing rescan or wait.");
        }
        CWallet::ScanResult result = pwallet->ScanForWalletTransactions(pwallet->chain().getBlockHash(0), {}, reserver, true);
        switch (result.status) {
        case CWallet::ScanResult::SUCCESS:
            break;
        case CWallet::ScanResult::FAILURE:
            throw JSONRPCError(RPC_MISC_ERROR, "Rescan failed. Potentially corrupted data files.");
        case CWallet::ScanResult::USER_ABORT:
            throw JSONRPCError(RPC_MISC_ERROR, "Rescan aborted.");
            // no default case, so the compiler can warn about missing cases
        }
    }

    return true;
}

static UniValue loadwallet(const JSONRPCRequest& request)
{
    RPCHelpMan{"loadwallet",
        "\nLoads a wallet from a wallet file or directory."
        "\nNote that all wallet command-line options used when starting blocxd will be"
        "\napplied to the new wallet (eg, rescan, etc).\n",
        {
            {"filename", RPCArg::Type::STR, RPCArg::Optional::NO, "The wallet directory or .dat file."},
            {"load_on_startup", RPCArg::Type::BOOL, /* default */ "null", "Save wallet name to persistent settings and load on startup. True to add wallet to startup list, false to remove, null to leave unchanged."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "name", "The wallet name if loaded successfully."},
                {RPCResult::Type::STR, "warning", "Warning message if wallet was not loaded cleanly."},
            }
        },
        RPCExamples{
            HelpExampleCli("loadwallet", "\"test.dat\"")
    + HelpExampleRpc("loadwallet", "\"test.dat\"")
        },
    }.Check(request);

    WalletContext& context = EnsureWalletContext(request.context);
    const std::string name(request.params[0].get_str());

    DatabaseOptions options;
    DatabaseStatus status;
    options.require_existing = true;
    bilingual_str error;
    std::vector<bilingual_str> warnings;
    std::optional<bool> load_on_start = request.params[1].isNull() ? std::nullopt : std::optional<bool>(request.params[1].get_bool());
    std::shared_ptr<CWallet> const wallet = LoadWallet(*context.chain, name, load_on_start, options, status, error, warnings);
    if (!wallet) {
        // Map bad format to not found, since bad format is returned when the
        // wallet directory exists, but doesn't contain a data file.
        RPCErrorCode code = status == DatabaseStatus::FAILED_NOT_FOUND || status == DatabaseStatus::FAILED_BAD_FORMAT ? RPC_WALLET_NOT_FOUND : RPC_WALLET_ERROR;
        throw JSONRPCError(code, error.original);
    }

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("name", wallet->GetName());
    obj.pushKV("warning", Join(warnings, Untranslated("\n")).original);

    return obj;
}

static UniValue setwalletflag(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    std::string flags = "";
    for (auto& it : WALLET_FLAG_MAP)
        if (it.second & MUTABLE_WALLET_FLAGS)
            flags += (flags == "" ? "" : ", ") + it.first;

    RPCHelpMan{"setwalletflag",
        "\nChange the state of the given wallet flag for a wallet.\n",
        {
            {"flag", RPCArg::Type::STR, RPCArg::Optional::NO, "The name of the flag to change. Current available flags: " + flags},
            {"value", RPCArg::Type::BOOL, /* default */ "true", "The new state."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "flag_name", "The name of the flag that was modified"},
                {RPCResult::Type::BOOL, "flag_state", "The new state of the flag"},
                {RPCResult::Type::STR, "warnings", "Any warnings associated with the change"},
            }
        },
        RPCExamples{
            HelpExampleCli("setwalletflag", "avoid_reuse")
      + HelpExampleRpc("setwalletflag", "\"avoid_reuse\"")
        },
    }.Check(request);

    std::string flag_str = request.params[0].get_str();
    bool value = request.params[1].isNull() || request.params[1].get_bool();

    if (!WALLET_FLAG_MAP.count(flag_str)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Unknown wallet flag: %s", flag_str));
    }

    auto flag = WALLET_FLAG_MAP.at(flag_str);

    if (!(flag & MUTABLE_WALLET_FLAGS)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Wallet flag is immutable: %s", flag_str));
    }

    UniValue res(UniValue::VOBJ);

    if (pwallet->IsWalletFlagSet(flag) == value) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Wallet flag is already set to %s: %s", value ? "true" : "false", flag_str));
    }

    res.pushKV("flag_name", flag_str);
    res.pushKV("flag_state", value);

    if (value) {
        pwallet->SetWalletFlag(flag);
    } else {
        pwallet->UnsetWalletFlag(flag);
    }

    if (flag && value && WALLET_FLAG_CAVEATS.count(flag)) {
        res.pushKV("warnings", WALLET_FLAG_CAVEATS.at(flag));
    }

    return res;
}

static UniValue createwallet(const JSONRPCRequest& request)
{
    RPCHelpMan{
        "createwallet",
        "\nCreates and loads a new wallet.\n",
        {
            {"wallet_name", RPCArg::Type::STR, RPCArg::Optional::NO, "The name for the new wallet. If this is a path, the wallet will be created at the path location."},
            {"disable_private_keys", RPCArg::Type::BOOL, /* default */ "false", "Disable the possibility of private keys (only watchonlys are possible in this mode)."},
            {"blank", RPCArg::Type::BOOL, /* default */ "false", "Create a blank wallet. A blank wallet has no keys or HD seed. One can be set using upgradetohd."},
            {"passphrase", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Encrypt the wallet with this passphrase."},
            {"avoid_reuse", RPCArg::Type::BOOL, /* default */ "false", "Keep track of coin reuse, and treat dirty and clean coins differently with privacy considerations in mind."},
            {"load_on_startup", RPCArg::Type::BOOL, /* default */ "null", "Save wallet name to persistent settings and load on startup. True to add wallet to startup list, false to remove, null to leave unchanged."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "name", "The wallet name if created successfully. If the wallet was created using a full path, the wallet_name will be the full path."},
                {RPCResult::Type::STR, "warning", "Warning message if wallet was not loaded cleanly."},
            }
        },
        RPCExamples{
            HelpExampleCli("createwallet", "\"testwallet\"")
            + HelpExampleRpc("createwallet", "\"testwallet\"")
        },
    }.Check(request);

    WalletContext& context = EnsureWalletContext(request.context);
    uint64_t flags = 0;
    if (!request.params[1].isNull() && request.params[1].get_bool()) {
        flags |= WALLET_FLAG_DISABLE_PRIVATE_KEYS;
    }

    if (!request.params[2].isNull() && request.params[2].get_bool()) {
        flags |= WALLET_FLAG_BLANK_WALLET;
    }
    SecureString passphrase;
    passphrase.reserve(100);
    std::vector<bilingual_str> warnings;
    if (!request.params[3].isNull()) {
        passphrase = request.params[3].get_str().c_str();
        if (passphrase.empty()) {
            // Empty string means unencrypted
            warnings.emplace_back(Untranslated("Empty string given as passphrase, wallet will not be encrypted."));
        }
    }

    if (!request.params[4].isNull() && request.params[4].get_bool()) {
        flags |= WALLET_FLAG_AVOID_REUSE;
    }

    DatabaseOptions options;
    DatabaseStatus status;
    options.require_create = true;
    options.create_flags = flags;
    options.create_passphrase = passphrase;
    bilingual_str error;
    std::optional<bool> load_on_start = request.params[5].isNull() ? std::nullopt : std::optional<bool>(request.params[5].get_bool());
    std::shared_ptr<CWallet> wallet = CreateWallet(*context.chain, request.params[0].get_str(), load_on_start, options, status, error, warnings);
    if (!wallet) {
        RPCErrorCode code = status == DatabaseStatus::FAILED_ENCRYPT ? RPC_WALLET_ENCRYPTION_FAILED : RPC_WALLET_ERROR;
        throw JSONRPCError(code, error.original);
    }

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("name", wallet->GetName());
    obj.pushKV("warning", Join(warnings, Untranslated("\n")).original);

    return obj;
}

static UniValue unloadwallet(const JSONRPCRequest& request)
{
    RPCHelpMan{"unloadwallet",
        "Unloads the wallet referenced by the request endpoint otherwise unloads the wallet specified in the argument.\n"
        "Specifying the wallet name on a wallet endpoint is invalid.",
        {
            {"wallet_name", RPCArg::Type::STR, /* default */ "the wallet name from the RPC request", "The name of the wallet to unload."},
            {"load_on_startup", RPCArg::Type::BOOL, /* default */ "null", "Save wallet name to persistent settings and load on startup. True to add wallet to startup list, false to remove, null to leave unchanged."},
        },
        RPCResult{RPCResult::Type::OBJ, "", "", {
            {RPCResult::Type::STR, "warning", "Warning message if wallet was not unloaded cleanly."},
        }},
        RPCExamples{
            HelpExampleCli("unloadwallet", "wallet_name")
    + HelpExampleRpc("unloadwallet", "wallet_name")
        },
    }.Check(request);

    std::string wallet_name;
    if (GetWalletNameFromJSONRPCRequest(request, wallet_name)) {
        if (!request.params[0].isNull()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot unload the requested wallet");
        }
    } else {
        wallet_name = request.params[0].get_str();
    }

    std::shared_ptr<CWallet> wallet = GetWallet(wallet_name);
    if (!wallet) {
        throw JSONRPCError(RPC_WALLET_NOT_FOUND, "Requested wallet does not exist or is not loaded");
    }

    // Release the "main" shared pointer and prevent further notifications.
    // Note that any attempt to load the same wallet would fail until the wallet
    // is destroyed (see CheckUniqueFileid).
    std::vector<bilingual_str> warnings;
    std::optional<bool> load_on_start = request.params[1].isNull() ? std::nullopt : std::optional<bool>(request.params[1].get_bool());
    if (!RemoveWallet(wallet, load_on_start, warnings)) {
        throw JSONRPCError(RPC_MISC_ERROR, "Requested wallet already unloaded");
    }

    UnloadWallet(std::move(wallet));

    UniValue result(UniValue::VOBJ);
    result.pushKV("warning", Join(warnings, Untranslated("\n")).original);
    return result;
}

static UniValue listunspent(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    RPCHelpMan{"listunspent",
        "\nReturns array of unspent transaction outputs\n"
        "with between minconf and maxconf (inclusive) confirmations.\n"
        "Optionally filter to only include txouts paid to specified addresses.\n",
        {
            {"minconf", RPCArg::Type::NUM, /* default */ "1", "The minimum confirmations to filter"},
            {"maxconf", RPCArg::Type::NUM, /* default */ "9999999", "The maximum confirmations to filter"},
            {"addresses", RPCArg::Type::ARR, /* default */ "empty array", "The blocx addresses to filter",
                {
                    {"address", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "blocx address"},
                },
            },
            {"include_unsafe", RPCArg::Type::BOOL, /* default */ "true", "Include outputs that are not safe to spend\n"
    "                  See description of \"safe\" attribute below."},
            {"query_options", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED_NAMED_ARG, "JSON with query options",
                {
                    {"minimumAmount", RPCArg::Type::AMOUNT, /* default */ "0", "Minimum value of each UTXO in " + CURRENCY_UNIT + ""},
                    {"maximumAmount", RPCArg::Type::AMOUNT, /* default */ "unlimited", "Maximum value of each UTXO in " + CURRENCY_UNIT + ""},
                    {"maximumCount", RPCArg::Type::NUM, /* default */ "unlimited", "Maximum number of UTXOs"},
                    {"minimumSumAmount", RPCArg::Type::AMOUNT, /* default */ "unlimited", "Minimum sum value of all UTXOs in " + CURRENCY_UNIT + ""},
                    {"coinType", RPCArg::Type::NUM, /* default */ "0", "Filter coinTypes as follows:\n"
    "                         0=ALL_COINS, 1=ONLY_FULLY_MIXED, 2=ONLY_READY_TO_MIX, 3=ONLY_NONDENOMINATED,\n"
    "                         4=ONLY_MASTERNODE_COLLATERAL, 5=ONLY_COINJOIN_COLLATERAL" },
                },
                "query_options"},
        },
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {
                {RPCResult::Type::OBJ, "", "",
                {
                    {RPCResult::Type::STR_HEX, "txid", "the transaction id"},
                    {RPCResult::Type::NUM, "vout", "the vout value"},
                    {RPCResult::Type::STR, "address", "the blocx address"},
                    {RPCResult::Type::STR, "label", "The associated label, or \"\" for the default label"},
                    {RPCResult::Type::STR, "scriptPubKey", "the script key"},
                    {RPCResult::Type::STR_AMOUNT, "amount", "the transaction output amount in " + CURRENCY_UNIT},
                    {RPCResult::Type::NUM, "confirmations", "The number of confirmations"},
                    {RPCResult::Type::STR_HEX, "redeemScript", "The redeemScript if scriptPubKey is P2SH"},
                    {RPCResult::Type::BOOL, "spendable", "Whether we have the private keys to spend this output"},
                    {RPCResult::Type::BOOL, "solvable", "Whether we know how to spend this output, ignoring the lack of keys"},
                    {RPCResult::Type::STR, "desc", "(only when solvable) A descriptor for spending this output"},
                    {RPCResult::Type::BOOL, "reused", /* optional*/ true, "(only present if avoid_reuse is set) Whether this output is reused/dirty (sent to an address that was previously spent from)"},
                    {RPCResult::Type::BOOL, "safe", "Whether this output is considered safe to spend. Unconfirmed transactions"
                                                    "                             from outside keys and unconfirmed replacement transactions are considered unsafe\n"
                                                    "and are not eligible for spending by fundrawtransaction and sendtoaddress."},
                    {RPCResult::Type::NUM, "coinjoin_rounds", "The number of CoinJoin rounds"},
                }},
            }},
        RPCExamples{
            HelpExampleCli("listunspent", "")
    + HelpExampleCli("listunspent", "6 9999999 \"[\\\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\\\",\\\"XuQQkwA4FYkq2XERzMY2CiAZhJTEDAbtcg\\\"]\"")
    + HelpExampleRpc("listunspent", "6, 9999999 \"[\\\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\\\",\\\"XuQQkwA4FYkq2XERzMY2CiAZhJTEDAbtcg\\\"]\"")
    + HelpExampleCli("listunspent", "6 9999999 '[]' true '{ \"minimumAmount\": 0.005 }'")
    + HelpExampleRpc("listunspent", "6, 9999999, [] , true, { \"minimumAmount\": 0.005 } ")
        },
    }.Check(request);

    int nMinDepth = 1;
    if (!request.params[0].isNull()) {
        RPCTypeCheckArgument(request.params[0], UniValue::VNUM);
        nMinDepth = request.params[0].get_int();
    }

    int nMaxDepth = 9999999;
    if (!request.params[1].isNull()) {
        RPCTypeCheckArgument(request.params[1], UniValue::VNUM);
        nMaxDepth = request.params[1].get_int();
    }

    std::set<CTxDestination> destinations;
    if (!request.params[2].isNull()) {
        RPCTypeCheckArgument(request.params[2], UniValue::VARR);
        UniValue inputs = request.params[2].get_array();
        for (unsigned int idx = 0; idx < inputs.size(); idx++) {
            const UniValue& input = inputs[idx];
            CTxDestination dest = DecodeDestination(input.get_str());
            if (!IsValidDestination(dest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid BLOCX address: ") + input.get_str());
            }
            if (!destinations.insert(dest).second) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ") + input.get_str());
            }
        }
    }

    bool include_unsafe = true;
    if (!request.params[3].isNull()) {
        RPCTypeCheckArgument(request.params[3], UniValue::VBOOL);
        include_unsafe = request.params[3].get_bool();
    }

    CAmount nMinimumAmount = 0;
    CAmount nMaximumAmount = MAX_MONEY;
    CAmount nMinimumSumAmount = MAX_MONEY;
    uint64_t nMaximumCount = 0;
    CCoinControl coinControl;
    coinControl.nCoinType = CoinType::ALL_COINS;

    if (!request.params[4].isNull()) {
        const UniValue& options = request.params[4].get_obj();

        // Note: Keep this vector up to date with the options processed below
        const std::vector<std::string> vecOptions {
            "minimumAmount",
            "maximumAmount",
            "minimumSumAmount",
            "maximumCount",
            "coinType"
        };

        for (const auto& key : options.getKeys()) {
            if (std::find(vecOptions.begin(), vecOptions.end(), key) == vecOptions.end()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid key used in query_options JSON object: ") + key);
            }
        }

        if (options.exists("minimumAmount"))
            nMinimumAmount = AmountFromValue(options["minimumAmount"]);

        if (options.exists("maximumAmount"))
            nMaximumAmount = AmountFromValue(options["maximumAmount"]);

        if (options.exists("minimumSumAmount"))
            nMinimumSumAmount = AmountFromValue(options["minimumSumAmount"]);

        if (options.exists("maximumCount"))
            nMaximumCount = options["maximumCount"].get_int64();

        if (options.exists("coinType")) {
            int64_t nCoinType = options["coinType"].get_int64();

            if (nCoinType < static_cast<int64_t>(CoinType::MIN_COIN_TYPE) || nCoinType > static_cast<int64_t>(CoinType::MAX_COIN_TYPE)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid coinType selected. Available range: %d - %d", static_cast<int64_t>(CoinType::MIN_COIN_TYPE), static_cast<int64_t>(CoinType::MAX_COIN_TYPE)));
            }

            coinControl.nCoinType = static_cast<CoinType>(nCoinType);
        }
    }

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    UniValue results(UniValue::VARR);
    std::vector<COutput> vecOutputs;
    {
        coinControl.m_avoid_address_reuse = false;

        LOCK(pwallet->cs_wallet);
        pwallet->AvailableCoins(vecOutputs, !include_unsafe, &coinControl, nMinimumAmount, nMaximumAmount, nMinimumSumAmount, nMaximumCount, nMinDepth, nMaxDepth);
    }

    LOCK(pwallet->cs_wallet);

    const bool avoid_reuse = pwallet->IsWalletFlagSet(WALLET_FLAG_AVOID_REUSE);

    for (const COutput& out : vecOutputs) {
        CTxDestination address;
        const CScript& scriptPubKey = out.tx->tx->vout[out.i].scriptPubKey;
        bool fValidAddress = ExtractDestination(scriptPubKey, address);
        bool reused = avoid_reuse && pwallet->IsSpentKey(out.tx->GetHash(), out.i);

        if (destinations.size() && (!fValidAddress || !destinations.count(address)))
            continue;

        UniValue entry(UniValue::VOBJ);
        entry.pushKV("txid", out.tx->GetHash().GetHex());
        entry.pushKV("vout", out.i);

        if (fValidAddress) {
            entry.pushKV("address", EncodeDestination(address));

            auto i = pwallet->mapAddressBook.find(address);
            if (i != pwallet->mapAddressBook.end()) {
                entry.pushKV("label", i->second.name);
            }

            const SigningProvider* provider = pwallet->GetSigningProvider(scriptPubKey);
            if (provider) {
                if (scriptPubKey.IsPayToScriptHash()) {
                    const CScriptID& hash = CScriptID(std::get<ScriptHash>(address));
                    CScript redeemScript;
                    if (provider->GetCScript(hash, redeemScript)) {
                        entry.pushKV("redeemScript", HexStr(redeemScript));
                    }
                }
            }
        }

        entry.pushKV("scriptPubKey", HexStr(scriptPubKey));
        entry.pushKV("amount", ValueFromAmount(out.tx->tx->vout[out.i].nValue));
        entry.pushKV("confirmations", out.nDepth);
        entry.pushKV("spendable", out.fSpendable);
        entry.pushKV("solvable", out.fSolvable);
        if (out.fSolvable) {
            const SigningProvider* provider = pwallet->GetSigningProvider(scriptPubKey);
            if (provider) {
                auto descriptor = InferDescriptor(scriptPubKey, *provider);
                entry.pushKV("desc", descriptor->ToString());
            }
        }
        if (avoid_reuse) entry.pushKV("reused", reused);
        entry.pushKV("safe", out.fSafe);
        entry.pushKV("coinjoin_rounds", pwallet->GetRealOutpointCoinJoinRounds(COutPoint(out.tx->GetHash(), out.i)));
        results.push_back(entry);
    }

    return results;
}

void FundTransaction(CWallet* const pwallet, CMutableTransaction& tx, CAmount& fee_out, int& change_position, UniValue options)
{
    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    CCoinControl coinControl;
    change_position = -1;
    bool lockUnspents = false;
    UniValue subtractFeeFromOutputs;
    std::set<int> setSubtractFeeFromOutputs;

    if (!options.isNull()) {
      if (options.type() == UniValue::VBOOL) {
        // backward compatibility bool only fallback
        coinControl.fAllowWatchOnly = options.get_bool();
      }
      else {
        RPCTypeCheckArgument(options, UniValue::VOBJ);
        RPCTypeCheckObj(options,
            {
                {"changeAddress", UniValueType(UniValue::VSTR)},
                {"changePosition", UniValueType(UniValue::VNUM)},
                {"includeWatching", UniValueType(UniValue::VBOOL)},
                {"lockUnspents", UniValueType(UniValue::VBOOL)},
                {"feeRate", UniValueType()}, // will be checked below
                {"subtractFeeFromOutputs", UniValueType(UniValue::VARR)},
                {"conf_target", UniValueType(UniValue::VNUM)},
                {"estimate_mode", UniValueType(UniValue::VSTR)},
            },
            true, true);

        if (options.exists("changeAddress")) {
            CTxDestination dest = DecodeDestination(options["changeAddress"].get_str());

            if (!IsValidDestination(dest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "changeAddress must be a valid blocx address");
            }

            coinControl.destChange = dest;
        }

        if (options.exists("changePosition"))
            change_position = options["changePosition"].get_int();

        coinControl.fAllowWatchOnly = ParseIncludeWatchonly(options["includeWatching"], *pwallet);

        if (options.exists("lockUnspents"))
            lockUnspents = options["lockUnspents"].get_bool();

        if (options.exists("feeRate"))
        {
            coinControl.m_feerate = CFeeRate(AmountFromValue(options["feeRate"]));
            coinControl.fOverrideFeeRate = true;
        }

        if (options.exists("subtractFeeFromOutputs"))
            subtractFeeFromOutputs = options["subtractFeeFromOutputs"].get_array();
        if (options.exists("conf_target")) {
            if (options.exists("feeRate")) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot specify both conf_target and feeRate");
            }
            coinControl.m_confirm_target = ParseConfirmTarget(options["conf_target"], pwallet->chain().estimateMaxBlocks());
        }
        if (options.exists("estimate_mode")) {
            if (options.exists("feeRate")) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot specify both estimate_mode and feeRate");
            }
            if (!FeeModeFromString(options["estimate_mode"].get_str(), coinControl.m_fee_mode)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid estimate_mode parameter");
            }
        }
      }
    } else {
        // if options is null and not a bool
        coinControl.fAllowWatchOnly = ParseIncludeWatchonly(NullUniValue, *pwallet);
    }

    if (tx.vout.size() == 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "TX must have at least one output");

    if (change_position != -1 && (change_position < 0 || (unsigned int)change_position > tx.vout.size()))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "changePosition out of bounds");

    for (unsigned int idx = 0; idx < subtractFeeFromOutputs.size(); idx++) {
        int pos = subtractFeeFromOutputs[idx].get_int();
        if (setSubtractFeeFromOutputs.count(pos))
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid parameter, duplicated position: %d", pos));
        if (pos < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid parameter, negative position: %d", pos));
        if (pos >= int(tx.vout.size()))
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid parameter, position too large: %d", pos));
        setSubtractFeeFromOutputs.insert(pos);
    }

    bilingual_str error;

    if (!pwallet->FundTransaction(tx, fee_out, change_position, error, lockUnspents, setSubtractFeeFromOutputs, coinControl)) {
        throw JSONRPCError(RPC_WALLET_ERROR, error.original);
    }
}

static UniValue fundrawtransaction(const JSONRPCRequest& request)
{
    RPCHelpMan{"fundrawtransaction",
                "\nAdd inputs to a transaction until it has enough in value to meet its out value.\n"
                "This will not modify existing inputs, and will add at most one change output to the outputs.\n"
                "No existing outputs will be modified unless \"subtractFeeFromOutputs\" is specified.\n"
                "Note that inputs which were signed may need to be resigned after completion since in/outputs have been added.\n"
                "The inputs added will not be signed, use signrawtransactionwithkey\n"
                " or signrawtransactionwithwallet for that.\n"
                "Note that all existing inputs must have their previous output transaction be in the wallet.\n"
                "Note that all inputs selected must be of standard form and P2SH scripts must be\n"
                "in the wallet using importaddress or addmultisigaddress (to calculate fees).\n"
                "You can see whether this is the case by checking the \"solvable\" field in the listunspent output.\n"
                "Only pay-to-pubkey, multisig, and P2SH versions thereof are currently supported for watch-only\n",
                {
                    {"hexstring", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The hex string of the raw transaction"},
                    {"options", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED_NAMED_ARG, "for backward compatibility: passing in a true instead of an object will result in {\"includeWatching\":true}",
                        {
                            {"changeAddress", RPCArg::Type::STR, /* default */ "pool address", "The blocx address to receive the change"},
                            {"changePosition", RPCArg::Type::NUM, /* default */ "random", "The index of the change output"},
                            {"includeWatching", RPCArg::Type::BOOL, /* default */ "true for watch-only wallets, otherwise false", "Also select inputs which are watch only.\n"
                                                          "Only solvable inputs can be used. Watch-only destinations are solvable if the public key and/or output script was imported,\n"
                                                          "e.g. with 'importpubkey' or 'importmulti' with the 'pubkeys' or 'desc' field."},
                            {"lockUnspents", RPCArg::Type::BOOL, /* default */ "false", "Lock selected unspent outputs"},
                            {"feeRate", RPCArg::Type::AMOUNT, /* default */ "not set: makes wallet determine the fee", "Set a specific fee rate in " + CURRENCY_UNIT + "/kB"},
                            {"subtractFeeFromOutputs", RPCArg::Type::ARR, /* default */ "empty array", "The integers.\n"
                            "                              The fee will be equally deducted from the amount of each specified output.\n"
                            "                              Those recipients will receive less blocx than you enter in their corresponding amount field.\n"
                            "                              If no outputs are specified here, the sender pays the fee.",
                                {
                                    {"vout_index", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "The zero-based output index, before a change output is added."},
                                },
                            },
                            {"conf_target", RPCArg::Type::NUM, /* default */ "wallet default", "Confirmation target (in blocks)"},
                            {"estimate_mode", RPCArg::Type::STR, /* default */ "UNSET", "The fee estimate mode, must be one of:\n"
                            "         \"UNSET\"\n"
                            "         \"ECONOMICAL\"\n"
                            "         \"CONSERVATIVE\""},
                        },
                        "options"},
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::STR_HEX, "hex", "The resulting raw transaction (hex-encoded string)"},
                        {RPCResult::Type::STR_AMOUNT, "fee", "Fee in " + CURRENCY_UNIT + " the resulting transaction pays"},
                        {RPCResult::Type::NUM, "changepos", "The position of the added change output, or -1"},
                    }
                                },
                                RPCExamples{
                            "\nCreate a transaction with no inputs\n"
                            + HelpExampleCli("createrawtransaction", "\"[]\" \"{\\\"myaddress\\\":0.01}\"") +
                            "\nAdd sufficient unsigned inputs to meet the output value\n"
                            + HelpExampleCli("fundrawtransaction", "\"rawtransactionhex\"") +
                            "\nSign the transaction\n"
                            + HelpExampleCli("signrawtransaction", "\"fundedtransactionhex\"") +
                            "\nSend the transaction\n"
                            + HelpExampleCli("sendrawtransaction", "\"signedtransactionhex\"")
                                },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValueType(), UniValue::VBOOL});

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    // parse hex string from parameter
    CMutableTransaction tx;
    if (!DecodeHexTx(tx, request.params[0].get_str())) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }

    CAmount fee;
    int change_position;
    FundTransaction(pwallet, tx, fee, change_position, request.params[1]);

    UniValue result(UniValue::VOBJ);
    result.pushKV("hex", EncodeHexTx(CTransaction(tx)));
    result.pushKV("fee", ValueFromAmount(fee));
    result.pushKV("changepos", change_position);

    return result;
}

UniValue signrawtransactionwithwallet(const JSONRPCRequest& request)
{
    RPCHelpMan{"signrawtransactionwithwallet",
        "\nSign inputs for raw transaction (serialized, hex-encoded).\n"
        "The second optional argument (may be null) is an array of previous transaction outputs that\n"
        "this transaction depends on but may not yet be in the block chain." +
                HELP_REQUIRING_PASSPHRASE,
        {
            {"hexstring", RPCArg::Type::STR, RPCArg::Optional::NO, "The transaction hex string"},
            {"prevtxs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG, "The previous dependent transaction outputs",
                {
                    {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                            {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                            {"scriptPubKey", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "script key"},
                            {"redeemScript", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "(required for P2SH or P2WSH)"},
                            {"amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "The amount spent"},
                        },
                    },
                },
            },
            {"sighashtype", RPCArg::Type::STR, /* default */ "ALL", "The signature hash type. Must be one of\n"
    "       \"ALL\"\n"
    "       \"NONE\"\n"
    "       \"SINGLE\"\n"
    "       \"ALL|ANYONECANPAY\"\n"
    "       \"NONE|ANYONECANPAY\"\n"
    "       \"SINGLE|ANYONECANPAY\""},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "hex", "The hex-encoded raw transaction with signature(s)"},
                {RPCResult::Type::BOOL, "complete", "If the transaction has a complete set of signatures"},
                {RPCResult::Type::ARR, "errors", "Script verification errors (if there are any)",
                {
                    {RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::STR_HEX, "txid", "The hash of the referenced, previous transaction"},
                        {RPCResult::Type::NUM, "vout", "The index of the output to spent and used as input"},
                        {RPCResult::Type::STR_HEX, "scriptSig", "The hex-encoded signature script"},
                        {RPCResult::Type::NUM, "sequence", "Script sequence number"},
                        {RPCResult::Type::STR, "error", "Verification or signing error related to the input"},
                    }},
                }},
            }
        },
        RPCExamples{
            HelpExampleCli("signrawtransactionwithwallet", "\"myhex\"")
    + HelpExampleRpc("signrawtransactionwithwallet", "\"myhex\"")
        },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VARR, UniValue::VSTR}, true);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    CMutableTransaction mtx;
    if (!DecodeHexTx(mtx, request.params[0].get_str())) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }

    // Sign the transaction
    LOCK(pwallet->cs_wallet);
    EnsureWalletIsUnlocked(pwallet);

    // Fetch previous transactions (inputs):
    std::map<COutPoint, Coin> coins;
    for (const CTxIn& txin : mtx.vin) {
        coins[txin.prevout]; // Create empty map entry keyed by prevout.
    }
    pwallet->chain().findCoins(coins);

    // Parse the prevtxs array
    ParsePrevouts(request.params[1], nullptr, coins);

    std::set<const SigningProvider*> providers;
    for (const std::pair<COutPoint, Coin> coin_pair : coins) {
        const SigningProvider* provider = pwallet->GetSigningProvider(coin_pair.second.out.scriptPubKey);
        if (provider) {
            providers.insert(std::move(provider));
        }
    }
    if (providers.size() == 0) {
        // When there are no available providers, use DUMMY_SIGNING_PROVIDER so we can check if the tx is complete
        providers.insert(&DUMMY_SIGNING_PROVIDER);
    }

    UniValue result(UniValue::VOBJ);
    for (const SigningProvider* provider : providers) {
        SignTransaction(mtx, provider, coins, request.params[2], result);
    }
     return result;
}

static UniValue rescanblockchain(const JSONRPCRequest& request)
{
    RPCHelpMan{"rescanblockchain",
        "\nRescan the local blockchain for wallet related transactions.\n"
        "Note: Use \"getwalletinfo\" to query the scanning progress.\n",
        {
            {"start_height", RPCArg::Type::NUM, /* default */ "0", "block height where the rescan should start"},
            {"stop_height", RPCArg::Type::NUM, RPCArg::Optional::OMITTED_NAMED_ARG, "the last block height that should be scanned. If none is provided it will rescan up to the tip at return time of this call."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::NUM, "start_height", "The block height where the rescan started (the requested height or 0)"},
                {RPCResult::Type::NUM, "stop_height", "The height of the last rescanned block. May be null in rare cases if there was a reorg and the call didn't scan any blocks because they were already scanned in the background."},
            }
        },
        RPCExamples{
            HelpExampleCli("rescanblockchain", "100000 120000")
    + HelpExampleRpc("rescanblockchain", "100000, 120000")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    WalletRescanReserver reserver(pwallet);
    if (!reserver.reserve()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is currently rescanning. Abort existing rescan or wait.");
    }

    int start_height = 0;
    uint256 start_block, stop_block;
    {
        std::optional<int> tip_height = pwallet->chain().getHeight();

        if (!request.params[0].isNull()) {
            start_height = request.params[0].get_int();
            if (start_height < 0 || !tip_height || start_height > *tip_height) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid start_height");
            }
        }

        std::optional<int> stop_height;
        if (!request.params[1].isNull()) {
            stop_height = request.params[1].get_int();
            if (*stop_height < 0 || !tip_height || *stop_height > *tip_height) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid stop_height");
            } else if (*stop_height < start_height) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "stop_height must be greater than start_height");
            }
        }

        // We can't rescan beyond non-pruned blocks, stop and throw an error
        if (pwallet->chain().findPruned(start_height, stop_height)) {
            throw JSONRPCError(RPC_MISC_ERROR, "Can't rescan beyond pruned data. Use RPC call getblockchaininfo to determine your pruned height.");
        }

        if (tip_height) {
            start_block = pwallet->chain().getBlockHash(start_height);
            // If called with a stop_height, set the stop_height here to
            // trigger a rescan to that height.
            // If called without a stop height, leave stop_height as null here
            // so rescan continues to the tip (even if the tip advances during
            // rescan).
            if (stop_height) {
                stop_block = pwallet->chain().getBlockHash(*stop_height);
            }
        }
    }

    CWallet::ScanResult result =
        pwallet->ScanForWalletTransactions(start_block, stop_block, reserver, true /* fUpdate */);
    switch (result.status) {
    case CWallet::ScanResult::SUCCESS:
        break;
    case CWallet::ScanResult::FAILURE:
        throw JSONRPCError(RPC_MISC_ERROR, "Rescan failed. Potentially corrupted data files.");
    case CWallet::ScanResult::USER_ABORT:
        throw JSONRPCError(RPC_MISC_ERROR, "Rescan aborted.");
        // no default case, so the compiler can warn about missing cases
    }
    UniValue response(UniValue::VOBJ);
    response.pushKV("start_height", start_height);
    response.pushKV("stop_height", result.last_scanned_height ? *result.last_scanned_height : UniValue());
    return response;
}

static UniValue wipewallettxes(const JSONRPCRequest& request)
{
    RPCHelpMan{"wipewallettxes",
        "\nWipe wallet transactions.\n"
        "Note: Use \"rescanblockchain\" to initiate the scanning progress and recover wallet transactions.\n",
        {
            {"keep_confirmed", RPCArg::Type::BOOL, /* default */ "false", "Do not wipe confirmed transactions"},
        },
        RPCResult{RPCResult::Type::NONE, "", ""},
        RPCExamples{
            HelpExampleCli("wipewallettxes", "")
    + HelpExampleRpc("wipewallettxes", "")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    WalletRescanReserver reserver(pwallet);
    if (!reserver.reserve()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is currently rescanning. Abort rescan or wait.");
    }

    LOCK(pwallet->cs_wallet);

    bool keep_confirmed{false};
    if (!request.params[0].isNull()) {
        keep_confirmed = request.params[0].get_bool();
    }

    const size_t WALLET_SIZE{pwallet->mapWallet.size()};
    const size_t STEPS{20};
    const size_t BATCH_SIZE = std::max(WALLET_SIZE / STEPS, size_t(1000));

    pwallet->ShowProgress(strprintf("%s " + _("Wiping wallet transactions...").translated, pwallet->GetDisplayName()), 0);

    for (size_t progress = 0; progress < STEPS; ++progress) {
        std::vector<uint256> vHashIn;
        std::vector<uint256> vHashOut;
        size_t count{0};

        for (auto& [txid, wtx] : pwallet->mapWallet) {
            if (progress < STEPS - 1 && ++count > BATCH_SIZE) break;
            if (keep_confirmed && wtx.m_confirm.status == CWalletTx::CONFIRMED) continue;
            vHashIn.push_back(txid);
        }

        if (vHashIn.size() > 0 && pwallet->ZapSelectTx(vHashIn, vHashOut) != DBErrors::LOAD_OK) {
            pwallet->ShowProgress(strprintf("%s " + _("Wiping wallet transactions...").translated, pwallet->GetDisplayName()), 100);
            throw JSONRPCError(RPC_WALLET_ERROR, "Could not properly delete transactions.");
        }

        CHECK_NONFATAL(vHashOut.size() == vHashIn.size());

        if (pwallet->IsAbortingRescan() || pwallet->chain().shutdownRequested()) {
            pwallet->ShowProgress(strprintf("%s " + _("Wiping wallet transactions...").translated, pwallet->GetDisplayName()), 100);
            throw JSONRPCError(RPC_MISC_ERROR, "Wiping was aborted by user.");
        }

        pwallet->ShowProgress(strprintf("%s " + _("Wiping wallet transactions...").translated, pwallet->GetDisplayName()), std::max(1, std::min(99, int(progress * 100 / STEPS))));
    }

    pwallet->ShowProgress(strprintf("%s " + _("Wiping wallet transactions...").translated, pwallet->GetDisplayName()), 100);

    return NullUniValue;
}

class DescribeWalletAddressVisitor
{
public:
    const SigningProvider * const provider;

    explicit DescribeWalletAddressVisitor(const SigningProvider * const _provider) : provider(_provider) {}

    UniValue operator()(const CNoDestination &dest) const { return UniValue(UniValue::VOBJ); }

    UniValue operator()(const PKHash& pkhash) const {
        CKeyID keyID(pkhash);
        UniValue obj(UniValue::VOBJ);
        CPubKey vchPubKey;
        if (provider && provider->GetPubKey(keyID, vchPubKey)) {
            obj.pushKV("pubkey", HexStr(vchPubKey));
            obj.pushKV("iscompressed", vchPubKey.IsCompressed());
        }
        return obj;
    }

    UniValue operator()(const ScriptHash& scriptHash) const {
        CScriptID scriptID(scriptHash);
        UniValue obj(UniValue::VOBJ);
        CScript subscript;
        if (provider && provider->GetCScript(scriptID, subscript)) {
            // Always present: script type and redeemscript
            std::vector<std::vector<unsigned char>> solutions_data;
            TxoutType whichType = Solver(subscript, solutions_data);
            obj.pushKV("script", GetTxnOutputType(whichType));
            obj.pushKV("hex", HexStr(subscript));
            if (whichType == TxoutType::MULTISIG) {
                // Also report some information on multisig scripts (which do not have a corresponding address).
                UniValue pubkeys(UniValue::VARR);
                UniValue addresses(UniValue::VARR);
                for (size_t i = 1; i < solutions_data.size() - 1; ++i) {
                    CPubKey pubkey(solutions_data[i]);
                    pubkeys.push_back(HexStr(pubkey));
                    addresses.push_back(EncodeDestination(PKHash(pubkey)));
                }
                obj.pushKV("pubkeys", std::move(pubkeys));
                obj.pushKV("addresses", std::move(addresses));
                obj.pushKV("sigsrequired", solutions_data[0][0]);
            }
        }
        return obj;
    }
};

static UniValue DescribeWalletAddress(CWallet* pwallet, const CTxDestination& dest)
{
    UniValue ret(UniValue::VOBJ);
    UniValue detail = DescribeAddress(dest);
    CScript script = GetScriptForDestination(dest);
    const SigningProvider* provider = nullptr;
    if (pwallet) {
        provider = pwallet->GetSigningProvider(script);
    }
    ret.pushKVs(detail);
    ret.pushKVs(std::visit(DescribeWalletAddressVisitor(provider), dest));
    return ret;
}

/** Convert CAddressBookData to JSON record.  */
static UniValue AddressBookDataToJSON(const CAddressBookData& data, const bool verbose)
{
    UniValue ret(UniValue::VOBJ);
    if (verbose) {
        ret.pushKV("name", data.name);
    }
    ret.pushKV("purpose", data.purpose);
    return ret;
}

UniValue getaddressinfo(const JSONRPCRequest& request)
{
    RPCHelpMan{"getaddressinfo",
        "\nReturn information about the given blocx address. Some information requires the address\n"
        "to be in the wallet.\n",
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The blocx address to get the information of."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "address", "The blocx address validated."},
                {RPCResult::Type::STR_HEX, "scriptPubKey", "The hex-encoded scriptPubKey generated by the address."},
                {RPCResult::Type::BOOL, "ismine", "If the address is yours."},
                {RPCResult::Type::BOOL, "iswatchonly", "If the address is watchonly."},
                {RPCResult::Type::BOOL, "solvable", "Whether we know how to spend coins sent to this address, ignoring the possible lack of private keys."},
                {RPCResult::Type::STR, "desc", /* optional */ true, "A descriptor for spending coins sent to this address (only when solvable)."},
                {RPCResult::Type::BOOL, "isscript", "If the key is a script."},
                {RPCResult::Type::BOOL, "ischange", "If the address was used for change output."},
                {RPCResult::Type::STR, "script", /* optional */ true, "The output script type. Only if \"isscript\" is true and the redeemscript is known. Possible types: nonstandard, pubkey, pubkeyhash, scripthash, multisig, nulldata"},
                {RPCResult::Type::STR_HEX, "hex", /* optional */ true, "The redeemscript for the p2sh address."},
                {RPCResult::Type::ARR, "pubkeys", /* optional */ true, "Array of pubkeys associated with the known redeemscript (only if \"script\" is \"multisig\").",
                {
                    {RPCResult::Type::STR, "pubkey", ""},
                }},
                {RPCResult::Type::ARR, "addresses", /* optional */ true, "Array of addresses associated with the known redeemscript (only if \"script\" is \"multisig\").",
                {
                    {RPCResult::Type::STR, "address", ""},
                }},
                {RPCResult::Type::NUM, "sigsrequired", /* optional */ true, "The number of signatures required to spend multisig output (only if \"script\" is \"multisig\")."},
                {RPCResult::Type::STR_HEX, "pubkey", /* optional */ true, "The hex value of the raw public key, for single-key addresses."},
                {RPCResult::Type::BOOL, "iscompressed", /* optional */ true, "If the pubkey is compressed."},
                {RPCResult::Type::STR, "label", "The label associated with the address, \"\" is the default label."},
                {RPCResult::Type::NUM_TIME, "timestamp", /* optional */ true, "The creation time of the key, if available, expressed in " + UNIX_EPOCH_TIME + "."},
                {RPCResult::Type::STR_HEX, "hdchainid", /* optional */ true, "The ID of the HD chain."},
                {RPCResult::Type::STR, "hdkeypath", /* optional */ true, "The HD keypath, if the key is HD and available."},
                {RPCResult::Type::STR_HEX, "hdseedid", /* optional */ true, "The Hash160 of the HD seed."},
                {RPCResult::Type::STR_HEX, "hdmasterfingerprint", /* optional */ true, "The fingerprint of the master key."},
                {RPCResult::Type::ARR, "labels", "Array of labels associated with the address.",
                {
                    {RPCResult::Type::STR, "label name", "The label name. Defaults to \"\"."},
                    {RPCResult::Type::OBJ, "", "json object of label data",
                    {
                        {RPCResult::Type::STR, "name", "The label."},
                        {RPCResult::Type::STR, "purpose", "Purpose of address (\"send\" for sending address, \"receive\" for receiving address)"},
                    }},
                }},
            }
        },
        RPCExamples{
            HelpExampleCli("getaddressinfo", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\"")
    + HelpExampleRpc("getaddressinfo", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\"")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    LOCK(pwallet->cs_wallet);

    UniValue ret(UniValue::VOBJ);
    CTxDestination dest = DecodeDestination(request.params[0].get_str());

    // Make sure the destination is valid
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    std::string currentAddress = EncodeDestination(dest);
    ret.pushKV("address", currentAddress);

    CScript scriptPubKey = GetScriptForDestination(dest);
    ret.pushKV("scriptPubKey", HexStr(scriptPubKey));
    const SigningProvider* provider = pwallet->GetSigningProvider(scriptPubKey);

    isminetype mine = pwallet->IsMine(dest);
    ret.pushKV("ismine", bool(mine & ISMINE_SPENDABLE));
    bool solvable = provider && IsSolvable(*provider, scriptPubKey);
    ret.pushKV("solvable", solvable);
    if (solvable) {
        ret.pushKV("desc", InferDescriptor(scriptPubKey, *provider)->ToString());
    }
    ret.pushKV("iswatchonly", bool(mine & ISMINE_WATCH_ONLY));
    UniValue detail = DescribeWalletAddress(pwallet, dest);
    ret.pushKVs(detail);
    if (pwallet->mapAddressBook.count(dest)) {
        ret.pushKV("label", pwallet->mapAddressBook[dest].name);
    }
    ret.pushKV("ischange", pwallet->IsChange(scriptPubKey));

    ScriptPubKeyMan* spk_man = pwallet->GetScriptPubKeyMan(scriptPubKey);
    if (spk_man) {
        const PKHash *pkhash = std::get_if<PKHash>(&dest);
        if (const CKeyMetadata* meta = spk_man->GetMetadata(dest)) {
            ret.pushKV("timestamp", meta->nCreateTime);
            CHDChain hdChainCurrent;
            LegacyScriptPubKeyMan* legacy_spk_man = pwallet->GetLegacyScriptPubKeyMan();
            if (legacy_spk_man != nullptr) {
                LOCK(pwallet->cs_KeyStore);
                AssertLockHeld(legacy_spk_man->cs_KeyStore);
                if (pkhash && pwallet->mapHdPubKeys.count(CKeyID(*pkhash)) && legacy_spk_man->GetHDChain(hdChainCurrent)) {
                    ret.pushKV("hdchainid", hdChainCurrent.GetID().GetHex());
                }
            }
            if (meta->has_key_origin) {
                ret.pushKV("hdkeypath", WriteHDKeypath(meta->key_origin.path));
                ret.pushKV("hdmasterfingerprint", HexStr(meta->key_origin.fingerprint));
            }
        }
    }

    // Currently only one label can be associated with an address, return an array
    // so the API remains stable if we allow multiple labels to be associated with
    // an address.
    UniValue labels(UniValue::VARR);
    std::map<CTxDestination, CAddressBookData>::iterator mi = pwallet->mapAddressBook.find(dest);
    if (mi != pwallet->mapAddressBook.end()) {
        labels.push_back(AddressBookDataToJSON(mi->second, true));
    }
    ret.pushKV("labels", std::move(labels));

    return ret;
}

static UniValue getaddressesbylabel(const JSONRPCRequest& request)
{
    RPCHelpMan{"getaddressesbylabel",
        "\nReturns the list of addresses assigned the specified label.\n",
        {
            {"label", RPCArg::Type::STR, RPCArg::Optional::NO, "The label."},
        },
        RPCResult{
            RPCResult::Type::OBJ_DYN, "", "json object with addresses as keys",
            {
                {RPCResult::Type::OBJ, "address", "json object with information about address",
                {
                    {RPCResult::Type::STR, "purpose", "Purpose of address (\"send\" for sending address, \"receive\" for receiving address)"},
                }},
            }
        },
        RPCExamples{
            HelpExampleCli("getaddressesbylabel", "\"tabby\"")
    + HelpExampleRpc("getaddressesbylabel", "\"tabby\"")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    LOCK(pwallet->cs_wallet);

    std::string label = LabelFromValue(request.params[0]);

    // Find all addresses that have the given label
    UniValue ret(UniValue::VOBJ);
    std::set<std::string> addresses;
    for (const std::pair<CTxDestination, CAddressBookData> item : pwallet->mapAddressBook) {
        if (item.second.name == label) {
            std::string address = EncodeDestination(item.first);
            // CWallet::mapAddressBook is not expected to contain duplicate
            // address strings, but build a separate set as a precaution just in
            // case it does.
            bool unique = addresses.emplace(address).second;
            CHECK_NONFATAL(unique);
            // UniValue::pushKV checks if the key exists in O(N)
            // and since duplicate addresses are unexpected (checked with
            // std::set in O(log(N))), UniValue::__pushKV is used instead,
            // which currently is O(1).
            ret.__pushKV(address, AddressBookDataToJSON(item.second, false));
        }
    }

    if (ret.empty()) {
        throw JSONRPCError(RPC_WALLET_INVALID_LABEL_NAME, std::string("No addresses with label " + label));
    }

    return ret;
}

static UniValue listlabels(const JSONRPCRequest& request)
{
    RPCHelpMan{"listlabels",
        "\nReturns the list of all labels, or labels that are assigned to addresses with a specific purpose.\n",
        {
            {"purpose", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, "Address purpose to list labels for ('send','receive'). An empty string is the same as not providing this argument."},
        },
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {
                {RPCResult::Type::STR, "label", "Label name"},
            }
        },
        RPCExamples{
    "\nList all labels\n"
    + HelpExampleCli("listlabels", "") +
    "\nList labels that have receiving addresses\n"
    + HelpExampleCli("listlabels", "receive") +
    "\nList labels that have sending addresses\n"
    + HelpExampleCli("listlabels", "send") +
    "\nAs a JSON-RPC call\n"
    + HelpExampleRpc("listlabels", "receive")
        },
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    LOCK(pwallet->cs_wallet);

    std::string purpose;
    if (!request.params[0].isNull()) {
        purpose = request.params[0].get_str();
    }

    // Add to a set to sort by label name, then insert into Univalue array
    std::set<std::string> label_set;
    for (const std::pair<CTxDestination, CAddressBookData> entry : pwallet->mapAddressBook) {
        if (purpose.empty() || entry.second.purpose == purpose) {
            label_set.insert(entry.second.name);
        }
    }

    UniValue ret(UniValue::VARR);
    for (const std::string& name : label_set) {
        ret.push_back(name);
    }

    return ret;
}

UniValue abortrescan(const JSONRPCRequest& request); // in rpcdump.cpp
UniValue dumpprivkey(const JSONRPCRequest& request); // in rpcdump.cpp
UniValue importprivkey(const JSONRPCRequest& request);
UniValue importaddress(const JSONRPCRequest& request);
UniValue importpubkey(const JSONRPCRequest& request);
UniValue dumpwallet(const JSONRPCRequest& request);
UniValue importwallet(const JSONRPCRequest& request);
UniValue importprunedfunds(const JSONRPCRequest& request);
UniValue removeprunedfunds(const JSONRPCRequest& request);
UniValue importmulti(const JSONRPCRequest& request);
UniValue dumphdinfo(const JSONRPCRequest& request);
UniValue importelectrumwallet(const JSONRPCRequest& request);

UniValue walletprocesspsbt(const JSONRPCRequest& request)
{
    RPCHelpMan{"walletprocesspsbt",
        "\nUpdate a PSBT with input information from our wallet and then sign inputs\n"
        "that we can sign for." +
                HELP_REQUIRING_PASSPHRASE,
        {
            {"psbt", RPCArg::Type::STR, RPCArg::Optional::NO, "The transaction base64 string"},
            {"sign", RPCArg::Type::BOOL, /* default */ "true", "Also sign the transaction when updating"},
            {"sighashtype", RPCArg::Type::STR, /* default */ "ALL", "The signature hash type to sign with if not specified by the PSBT. Must be one of\n"
    "       \"ALL\"\n"
    "       \"NONE\"\n"
    "       \"SINGLE\"\n"
    "       \"ALL|ANYONECANPAY\"\n"
    "       \"NONE|ANYONECANPAY\"\n"
    "       \"SINGLE|ANYONECANPAY\""},
            {"bip32derivs", RPCArg::Type::BOOL, /* default */ "true", "Include BIP 32 derivation paths for public keys if we know them"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "psbt", "The base64-encoded partially signed transaction"},
                {RPCResult::Type::BOOL, "complete", "If the transaction has a complete set of signatures"},
            }
        },
        RPCExamples{
            HelpExampleCli("walletprocesspsbt", "\"psbt\"")
        },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VBOOL, UniValue::VSTR});

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    // Unserialize the transaction
    PartiallySignedTransaction psbtx;
    std::string error;
    if (!DecodeBase64PSBT(psbtx, request.params[0].get_str(), error)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed %s", error));
    }

    // Get the sighash type
    int nHashType = ParseSighashString(request.params[2]);

    // Use CTransaction for the constant parts of the
    // transaction to avoid rehashing.
    const CTransaction txConst(*psbtx.tx);

    // Fill transaction with our data and also sign
    bool sign = request.params[1].isNull() ? true : request.params[1].get_bool();
    bool bip32derivs = request.params[3].isNull() ? true : request.params[3].get_bool();
    bool complete = true;
    const TransactionError err = FillPSBT(pwallet, psbtx, complete, nHashType, sign, bip32derivs);
    if (err != TransactionError::OK) {
        throw JSONRPCTransactionError(err);
    }

    UniValue result(UniValue::VOBJ);
    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << psbtx;
    result.pushKV("psbt", EncodeBase64(ssTx.str()));
    result.pushKV("complete", complete);

    return result;
}

UniValue walletcreatefundedpsbt(const JSONRPCRequest& request)
{
    RPCHelpMan{"walletcreatefundedpsbt",
        "\nCreates and funds a transaction in the Partially Signed Transaction format. Inputs will be added if supplied inputs are not enough\n"
        "Implements the Creator and Updater roles.\n",
        {
            {"inputs", RPCArg::Type::ARR, RPCArg::Optional::NO, "The inputs",
                {
                    {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                            {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                            {"sequence", RPCArg::Type::NUM, /* default */ "depends on the value of the 'locktime' argument", "The sequence number"},
                        },
                    },
                },
            },
            {"outputs", RPCArg::Type::ARR, RPCArg::Optional::NO, "The outputs (key-value pairs), where none of the keys are duplicated.\n"
                    "That is, each address can only appear once and there can only be one 'data' object.\n"
                    "For compatibility reasons, a dictionary, which holds the key-value pairs directly, is also\n"
                    "                             accepted as second parameter.",
                {
                    {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"address", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "A key-value pair. The key (string) is the blocx address, the value (float or string) is the amount in " + CURRENCY_UNIT + ""},
                        },
                        },
                    {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"data", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "A key-value pair. The key must be \"data\", the value is hex-encoded data"},
                        },
                    },
                },
            },
            {"locktime", RPCArg::Type::NUM, /* default */ "0", "Raw locktime. Non-0 value also locktime-activates inputs"},
            {"options", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED_NAMED_ARG, "",
                {
                    {"changeAddress", RPCArg::Type::STR_HEX, /* default */ "pool address", "The blocx address to receive the change"},
                    {"changePosition", RPCArg::Type::NUM, /* default */ "random", "The index of the change output"},
                    {"includeWatching", RPCArg::Type::BOOL, /* default */ "true for watch-only wallets, otherwise false", "Also select inputs which are watch only"},
                    {"lockUnspents", RPCArg::Type::BOOL, /* default */ "false", "Lock selected unspent outputs"},
                    {"feeRate", RPCArg::Type::AMOUNT, /* default */ "not set: makes wallet determine the fee", "Set a specific fee rate in " + CURRENCY_UNIT + "/kB"},
                    {"subtractFeeFromOutputs", RPCArg::Type::ARR, /* default */ "empty array", "The outputs to subtract the fee from.\n"
                    "                              The fee will be equally deducted from the amount of each specified output.\n"
                    "                              Those recipients will receive less BLOCX than you enter in their corresponding amount field.\n"
                    "                              If no outputs are specified here, the sender pays the fee.",
                        {
                            {"vout_index", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "The zero-based output index, before a change output is added."},
                        },
                    },
                    {"conf_target", RPCArg::Type::NUM, /* default */ "Fallback to wallet's confirmation target", "Confirmation target (in blocks)"},
                    {"estimate_mode", RPCArg::Type::STR, /* default */ "UNSET", "The fee estimate mode, must be one of:\n"
                    "         \"UNSET\"\n"
                    "         \"ECONOMICAL\"\n"
                    "         \"CONSERVATIVE\""},
                },
                "options"},
            {"bip32derivs", RPCArg::Type::BOOL, /* default */ "true", "Include BIP 32 derivation paths for public keys if we know them"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "psbt", "The resulting raw transaction (base64-encoded string)"},
                {RPCResult::Type::STR_AMOUNT, "fee", "Fee in " + CURRENCY_UNIT + " the resulting transaction pays"},
                {RPCResult::Type::NUM, "changepos", "The position of the added change output, or -1"},
            }
                        },
                        RPCExamples{
                    "\nCreate a transaction with no inputs\n"
                    + HelpExampleCli("walletcreatefundedpsbt", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"[{\\\"data\\\":\\\"00010203\\\"}]\"")
                        },
    }.Check(request);

    RPCTypeCheck(request.params, {
        UniValue::VARR,
        UniValueType(), // ARR or OBJ, checked later
        UniValue::VNUM,
        UniValue::VOBJ,
        UniValue::VBOOL,
        }, true
    );

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    CAmount fee;
    int change_position;
    CMutableTransaction rawTx = ConstructTransaction(request.params[0], request.params[1], request.params[2]);
    FundTransaction(pwallet, rawTx, fee, change_position, request.params[3]);

    // Make a blank psbt
    PartiallySignedTransaction psbtx{rawTx};

    // Fill transaction with out data but don't sign
    bool bip32derivs = request.params[4].isNull() ? true : request.params[4].get_bool();
    bool complete = true;
    const TransactionError err = FillPSBT(pwallet, psbtx, complete, 1, false, bip32derivs);
    if (err != TransactionError::OK) {
        throw JSONRPCTransactionError(err);
    }

    // Serialize the PSBT
    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << psbtx;

    UniValue result(UniValue::VOBJ);
    result.pushKV("psbt", EncodeBase64(ssTx.str()));
    result.pushKV("fee", ValueFromAmount(fee));
    result.pushKV("changepos", change_position);
    return result;
}

static UniValue upgradewallet(const JSONRPCRequest& request)
{
    RPCHelpMan{"upgradewallet",
        "\nUpgrade the wallet. Upgrades to the latest version if no version number is specified\n"
        "New keys may be generated and a new wallet backup will need to be made.",
        {
            {"version", RPCArg::Type::NUM, /* default */ strprintf("%d", FEATURE_LATEST), "The version number to upgrade to. Default is the latest wallet version"}
        },
        RPCResults{},
        RPCExamples{
            HelpExampleCli("upgradewallet", "120200")
            + HelpExampleRpc("upgradewallet", "120200")
        }
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    RPCTypeCheck(request.params, {UniValue::VNUM}, true);

    EnsureWalletIsUnlocked(pwallet);

    int version = 0;
    if (!request.params[0].isNull()) {
        version = request.params[0].get_int();
    }

    bilingual_str error;
    std::vector<bilingual_str> warnings;
    if (!pwallet->UpgradeWallet(version, error, warnings)) {
        throw JSONRPCError(RPC_WALLET_ERROR, error.original);
    }
    return error.original;
}

// clang-format off
static const CRPCCommand commands[] =
{ //  category              name                        actor (function)           argNames
    //  --------------------- ------------------------    -----------------------    ----------
    { "hidden",             "instantsendtoaddress",             &instantsendtoaddress,          {} },
    { "rawtransactions",    "fundrawtransaction",               &fundrawtransaction,            {"hexstring","options"} },
    { "wallet",             "abandontransaction",               &abandontransaction,            {"txid"} },
    { "wallet",             "abortrescan",                      &abortrescan,                   {} },
    { "wallet",             "addmultisigaddress",               &addmultisigaddress,            {"nrequired","keys","label"} },
    { "wallet",             "backupwallet",                     &backupwallet,                  {"destination"} },
    { "wallet",             "createwallet",                     &createwallet,                  {"wallet_name", "disable_private_keys", "blank", "passphrase", "avoid_reuse", "load_on_startup"} },
    { "wallet",             "dumphdinfo",                       &dumphdinfo,                    {} },
    { "wallet",             "dumpprivkey",                      &dumpprivkey,                   {"address"}  },
    { "wallet",             "dumpwallet",                       &dumpwallet,                    {"filename"} },
    { "wallet",             "encryptwallet",                    &encryptwallet,                 {"passphrase"} },
    { "wallet",             "getaddressesbylabel",              &getaddressesbylabel,           {"label"} },
    { "wallet",             "getaddressinfo",                   &getaddressinfo,                {"address"} },
    { "wallet",             "getbalance",                       &getbalance,                    {"dummy","minconf","addlocked","include_watchonly", "avoid_reuse"} },
    { "wallet",             "getnewaddress",                    &getnewaddress,                 {"label"} },
    { "wallet",             "getrawchangeaddress",              &getrawchangeaddress,           {} },
    { "wallet",             "getreceivedbyaddress",             &getreceivedbyaddress,          {"address","minconf","addlocked"} },
    { "wallet",             "getreceivedbylabel",               &getreceivedbylabel,            {"label","minconf","addlocked"} },
    { "wallet",             "gettransaction",                   &gettransaction,                {"txid","include_watchonly"} },
    { "wallet",             "getunconfirmedbalance",            &getunconfirmedbalance,         {} },
    { "wallet",             "getbalances",                      &getbalances,                   {} },
    { "wallet",             "getwalletinfo",                    &getwalletinfo,                 {} },
    { "wallet",             "importaddress",                    &importaddress,                 {"address","label","rescan","p2sh"} },
    { "wallet",             "importelectrumwallet",             &importelectrumwallet,          {"filename", "index"} },
    { "wallet",             "importmulti",                      &importmulti,                   {"requests","options"} },
    { "wallet",             "importprivkey",                    &importprivkey,                 {"privkey","label","rescan"} },
    { "wallet",             "importprunedfunds",                &importprunedfunds,             {"rawtransaction","txoutproof"} },
    { "wallet",             "importpubkey",                     &importpubkey,                  {"pubkey","label","rescan"} },
    { "wallet",             "importwallet",                     &importwallet,                  {"filename"} },
    { "wallet",             "keypoolrefill",                    &keypoolrefill,                 {"newsize"} },
    { "wallet",             "listaddressbalances",              &listaddressbalances,           {"minamount"} },
    { "wallet",             "listaddressgroupings",             &listaddressgroupings,          {} },
    { "wallet",             "listlabels",                       &listlabels,                    {"purpose"} },
    { "wallet",             "listlockunspent",                  &listlockunspent,               {} },
    { "wallet",             "listreceivedbyaddress",            &listreceivedbyaddress,         {"minconf","addlocked","include_empty","include_watchonly","address_filter"} },
    { "wallet",             "listreceivedbylabel",              &listreceivedbylabel,           {"minconf","addlocked","include_empty","include_watchonly"} },
    { "wallet",             "listsinceblock",                   &listsinceblock,                {"blockhash","target_confirmations","include_watchonly","include_removed"} },
    { "wallet",             "listtransactions",                 &listtransactions,              {"label|dummy","count","skip","include_watchonly"} },
    { "wallet",             "listunspent",                      &listunspent,                   {"minconf","maxconf","addresses","include_unsafe","query_options"} },
    { "wallet",             "listwalletdir",                    &listwalletdir,                 {} },
    { "wallet",             "listwallets",                      &listwallets,                   {} },
    { "wallet",             "loadwallet",                       &loadwallet,                    {"filename", "load_on_startup"} },
    { "wallet",             "lockunspent",                      &lockunspent,                   {"unlock","transactions"} },
    { "wallet",             "removeprunedfunds",                &removeprunedfunds,             {"txid"} },
    { "wallet",             "rescanblockchain",                 &rescanblockchain,              {"start_height", "stop_height"} },
    { "wallet",             "sendmany",                         &sendmany,                      {"dummy","amounts","minconf","addlocked","comment","subtractfeefrom","use_is","use_cj","conf_target","estimate_mode"} },
    { "wallet",             "sendtoaddress",                    &sendtoaddress,                 {"address","amount","comment","comment_to","subtractfeefromamount","use_is","use_cj","conf_target","estimate_mode", "avoid_reuse"} },
    { "wallet",             "setcoinjoinrounds",                &setcoinjoinrounds,             {"rounds"} },
    { "wallet",             "setcoinjoinamount",                &setcoinjoinamount,             {"amount"} },
    { "wallet",             "setlabel",                         &setlabel,                      {"address","label"} },
    { "wallet",             "settxfee",                         &settxfee,                      {"amount"} },
    { "wallet",             "setwalletflag",                    &setwalletflag,                 {"flag","value"} },
    { "wallet",             "signmessage",                      &signmessage,                   {"address","message"} },
    { "wallet",             "signrawtransactionwithwallet",     &signrawtransactionwithwallet,  {"hexstring","prevtxs","sighashtype"} },
    { "wallet",             "unloadwallet",                     &unloadwallet,                  {"wallet_name", "load_on_startup"} },
    { "wallet",             "upgradewallet",                    &upgradewallet,                 {"version"} },
    { "wallet",             "upgradetohd",                      &upgradetohd,                   {"mnemonic", "mnemonicpassphrase", "walletpassphrase", "rescan"} },
    { "wallet",             "walletlock",                       &walletlock,                    {} },
    { "wallet",             "walletpassphrasechange",           &walletpassphrasechange,        {"oldpassphrase","newpassphrase"} },
    { "wallet",             "walletpassphrase",                 &walletpassphrase,              {"passphrase","timeout","mixingonly"} },
    { "wallet",             "walletprocesspsbt",                &walletprocesspsbt,             {"psbt","sign","sighashtype","bip32derivs"} },
    { "wallet",             "walletcreatefundedpsbt",           &walletcreatefundedpsbt,        {"inputs","outputs","locktime","options","bip32derivs"} },
    { "wallet",             "wipewallettxes",                   &wipewallettxes,                {"keep_confirmed"} },
};
// clang-format on

Span<const CRPCCommand> GetWalletRPCCommands()
{
    return MakeSpan(commands);
}
