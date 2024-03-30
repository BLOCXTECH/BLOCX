// Copyright (c) 2023 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_EVO_DMN_TYPES_H
#define BITCOIN_EVO_DMN_TYPES_H

#include <amount.h>
#include <serialize.h>

#include <cassert>
#include <string_view>

enum class MnType : uint16_t {
    Standard_Masternode = 0,
    Lite = 1,
    COUNT,
    Invalid = std::numeric_limits<uint16_t>::max(),
};

template<> struct is_serializable_enum<MnType> : std::true_type {};

namespace dmn_types {

struct mntype_struct
{
    const int32_t voting_weight;
    const CAmount collat_amount;
    const std::string_view description;
};

constexpr auto Standard_Masternode = mntype_struct{
    .voting_weight = 10,
    .collat_amount = 100000 * COIN,
    .description = "Standard_Masternode",
};
constexpr auto Lite = mntype_struct{
    .voting_weight = 1,
    .collat_amount = 10000 * COIN,
    .description = "Lite",
};
constexpr auto Invalid = mntype_struct{
    .voting_weight = 0,
    .collat_amount = MAX_MONEY,
    .description = "Invalid",
};

[[nodiscard]] static constexpr bool IsCollateralAmount(CAmount amount)
{
    return amount == Standard_Masternode.collat_amount ||
        amount == Lite.collat_amount;
}

} // namespace dmn_types

[[nodiscard]] constexpr const dmn_types::mntype_struct GetMnType(MnType type)
{
    switch (type) {
        case MnType::Standard_Masternode: return dmn_types::Standard_Masternode;
        case MnType::Lite: return dmn_types::Lite;
        default: return dmn_types::Invalid;
    }
}

[[nodiscard]] constexpr const bool IsValidMnType(MnType type)
{
    return type < MnType::COUNT;
}

#endif // BITCOIN_EVO_DMN_TYPES_H
