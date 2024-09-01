#ifndef DIFFICULTY_ADJUSTMENT_H
#define DIFFICULTY_ADJUSTMENT_H

#include "includes.h"

using Height = int;

class DifficultyAdjustment {
public:
    static const boost::multiprecision::cpp_int PrecisionConstant;

    DifficultyAdjustment(const ChainSettings& chainSettings);

    Height nextRecalculationHeight(Height height, int epochLength) const;

    std::vector<Height> previousHeightsRequiredForRecalculation(Height height, int epochLength) const;

    std::vector<Height> heightsForNextRecalculation(Height height, int epochLength) const;

    difficulty bitcoinCalculate(const std::vector<Header>& previousHeaders, int epochLength) const;

    difficulty eip37Calculate(const std::vector<Header>& previousHeaders, int epochLength) const;

    BigInt interpolate(const std::vector<std::pair<int, difficulty>>& data, int epochLength) const;

    difficulty bitcoinCalculate(const Header& start, const Header& end, int epochLength) const;

    difficulty calculate(const std::vector<Header>& previousHeaders, int epochLength) const;

private:
    const ChainSettings& chainSettings;
    const uint64_t desiredInterval;
    const int useLastEpochs;
    const cpp_int initialDifficulty;
};

#endif // DIFFICULTY_ADJUSTMENT_H