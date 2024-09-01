#ifndef CHAIN_SETTINGS_H
#define CHAIN_SETTINGS_H

#include "includes.h"

using namespace boost::multiprecision;
using FiniteDuration = std::chrono::duration<int64_t, std::milli>;

class ChainSettings
{
public:
    ChainSettings(FiniteDuration blockInterval, int useLastEpochs, int epochLength, const std::string& initialDifficultyHex = "01");

    FiniteDuration getBlockInterval() const { return blockInterval; }
    int getUseLastEpochs() const { return useLastEpochs; }
    cpp_int getInitialDifficulty() const { return initialDifficulty; }
    int getEpochLength() const { return epochLength; }

private:
    FiniteDuration blockInterval;
    int useLastEpochs;
    int epochLength;
    std::string initialDifficultyHex;
    cpp_int initialDifficulty;

    cpp_int decodeHexToBigInt(const std::string& hex) const;
};

#endif // CHAIN_SETTINGS_H