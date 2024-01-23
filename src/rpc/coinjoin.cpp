// Copyright (c) 2019-2021 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation.h>
#ifdef ENABLE_WALLET
#include <coinjoin/coinjoin-client.h>
#include <coinjoin/coinjoin-client-options.h>
#include <wallet/rpcwallet.h>
#endif // ENABLE_WALLET
#include <coinjoin/coinjoin-server.h>
#include <rpc/server.h>
#include <version.h>

#include <univalue.h>

#ifdef ENABLE_WALLET
UniValue coinjoin(const JSONRPCRequest& request)
{
    return "This Method Has Been Disabled.";
}
#endif // ENABLE_WALLET

UniValue getpoolinfo(const JSONRPCRequest& request)
{
    return "This Method Has Been Disabled.";
}

UniValue getcoinjoininfo(const JSONRPCRequest& request)
{
    return "This Method Has Been Disabled.";
}

static const CRPCCommand commands[] =
    { //  category              name                      actor (function)         argNames
        //  --------------------- ------------------------  ---------------------------------
        { "blocx",               "getpoolinfo",            &getpoolinfo,            {} },
        { "blocx",               "getcoinjoininfo",        &getcoinjoininfo,        {} },
#ifdef ENABLE_WALLET
        { "blocx",               "coinjoin",               &coinjoin,               {} },
#endif // ENABLE_WALLET
};

void RegisterCoinJoinRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
