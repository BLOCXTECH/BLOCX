// Copyright (c) 2019-2022 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/context.h>
#include <validation.h>
#ifdef ENABLE_WALLET
#include <coinjoin/client.h>
#include <coinjoin/options.h>
#include <wallet/rpcwallet.h>
#endif // ENABLE_WALLET
#include <coinjoin/server.h>
#include <rpc/blockchain.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <util/strencodings.h>

#include <univalue.h>

#ifdef ENABLE_WALLET
static UniValue coinjoin(const JSONRPCRequest& request)
{
    return "This Method Has Been Disabled.";
}
#endif // ENABLE_WALLET

static UniValue getpoolinfo(const JSONRPCRequest& request)
{
    return "This Method Has Been Disabled.";
}

static UniValue getcoinjoininfo(const JSONRPCRequest& request)
{
    return "This Method Has Been Disabled.";
}
// clang-format off
static const CRPCCommand commands[] =
    { //  category              name                      actor (function)         argNames
        //  --------------------- ------------------------  ---------------------------------
        { "blocx",               "getpoolinfo",            &getpoolinfo,            {} },
        { "blocx",               "getcoinjoininfo",        &getcoinjoininfo,        {} },
#ifdef ENABLE_WALLET
        { "blocx",               "coinjoin",               &coinjoin,               {} },
#endif // ENABLE_WALLET
};
// clang-format on
void RegisterCoinJoinRPCCommands(CRPCTable &t)
{
    for (const auto& command : commands) {
        t.appendCommand(command.name, &command);
    }
}
