// Copyright (c) 2018-2022 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_EVO_SPECIALTX_H
#define BITCOIN_EVO_SPECIALTX_H

#include <consensus/validation.h>
#include <primitives/transaction.h>
#include <serialize.h>
#include <streams.h>
#include <uint256.h>
#include <version.h>

#include <string_view>
#include <vector>

struct maybe_error {
    bool did_err{false};
    ValidationInvalidReason reason{ValidationInvalidReason::CONSENSUS};
    std::string_view error_str;

    constexpr maybe_error() = default;
    constexpr maybe_error(ValidationInvalidReason reasonIn, std::string_view err): did_err(true), reason(reasonIn), error_str(err) {};
};

template <typename T>
inline bool GetTxPayload(const std::vector<unsigned char>& payload, T& obj)
{
    CDataStream ds(payload, SER_NETWORK, PROTOCOL_VERSION);
    try {
        ds >> obj;
    } catch (const std::exception& e) {
        return false;
    }
    return ds.empty();
}
template <typename T>
inline bool GetTxPayload(const CMutableTransaction& tx, T& obj, bool assert_type = true)
{
    if (assert_type) { ASSERT_IF_DEBUG(tx.nType == obj.SPECIALTX_TYPE); }
    return tx.nType == obj.SPECIALTX_TYPE && GetTxPayload(tx.vExtraPayload, obj);
}
template <typename T>
inline bool GetTxPayload(const CTransaction& tx, T& obj, bool assert_type = true)
{
    if (assert_type) { ASSERT_IF_DEBUG(tx.nType == obj.SPECIALTX_TYPE); }
    return tx.nType == obj.SPECIALTX_TYPE && GetTxPayload(tx.vExtraPayload, obj);
}

template <typename T>
void SetTxPayload(CMutableTransaction& tx, const T& payload)
{
    CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
    ds << payload;
    tx.vExtraPayload.assign(ds.begin(), ds.end());
}

uint256 CalcTxInputsHash(const CTransaction& tx);

#endif // BITCOIN_EVO_SPECIALTX_H
