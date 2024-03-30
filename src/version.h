// Copyright (c) 2012-2014 The Bitcoin Core developers
// Copyright (c) 2014-2023 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VERSION_H
#define BITCOIN_VERSION_H

/**
 * network protocol versioning
 */


static const int PROTOCOL_VERSION = 70228;

//! initial proto version, to be increased after version/verack negotiation
static const int INIT_PROTO_VERSION = 209;

//! disconnect from peers older than this proto version
static const int MIN_PEER_PROTO_VERSION = 70213;

//! minimum proto version of masternode to accept in DKGs
static const int MIN_MASTERNODE_PROTO_VERSION = 70219;

//! nTime field added to CAddress, starting with this version;
//! if possible, avoid requesting addresses nodes older than this
static const int CADDR_TIME_VERSION = 31402;

//! protocol version is included in MNAUTH starting with this version
static const int MNAUTH_NODE_VER_VERSION = 70218;

//! introduction of QGETDATA/QDATA messages
static const int LLMQ_DATA_MESSAGES_VERSION = 70219;

//! introduction of instant send deterministic lock (ISDLOCK)
static const int ISDLOCK_PROTO_VERSION = 70221;

//! GOVSCRIPT was activated in this version
static const int GOVSCRIPT_PROTO_VERSION = 70221;

//! ADDRV2 was introduced in this version
static const int ADDRV2_PROTO_VERSION = 70223;

//! CCoinJoinStatusUpdate bug fix was introduced in this version
static const int COINJOIN_SU_PROTO_VERSION = 70224;

//! BLS scheme was introduced in this version
static const int BLS_SCHEME_PROTO_VERSION = 70225;

//! DSQ and DSTX started using protx hash in this version
static const int COINJOIN_PROTX_HASH_PROTO_VERSION = 70226;

//! Masternode type was introduced in this version
static const int DMN_TYPE_PROTO_VERSION = 70227;

//! Versioned Simplified Masternode List Entries were introduced in this version
static const int SMNLE_VERSIONED_PROTO_VERSION = 70228;

static const int DISABLE_COINJOIN_PROTO_VERSION = 70220;
// Make sure that none of the values above collide with `ADDRV2_FORMAT`.

#endif // BITCOIN_VERSION_H
