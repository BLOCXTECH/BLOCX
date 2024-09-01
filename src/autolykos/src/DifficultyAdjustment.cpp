#include "includes.h"

const BigInt DifficultyAdjustment::PrecisionConstant = BigInt(1000000000);

DifficultyAdjustment::DifficultyAdjustment(const ChainSettings& chainSettings)
    : chainSettings(chainSettings),
      desiredInterval(std::chrono::duration_cast<std::chrono::milliseconds>(chainSettings.getBlockInterval()).count()),
      useLastEpochs(chainSettings.getUseLastEpochs()),
      initialDifficulty(chainSettings.getInitialDifficulty()) {
    if (useLastEpochs <= 1) {
        throw std::invalid_argument("useLastEpochs should always be > 1");
    }
    if (chainSettings.getEpochLength() <= 0) {
        throw std::invalid_argument("diff epoch length should always be > 0");
    }
    if (chainSettings.getEpochLength() >= INT_MAX / useLastEpochs) {
        throw std::invalid_argument("diff epoch length is too high for the specified number of epochs");
    }
}

Height DifficultyAdjustment::nextRecalculationHeight(Height height, int epochLength) const {
    if (height % epochLength == 0) {
        return height + 1;
    } else {
        return (height / epochLength + 1) * epochLength + 1;
    }
}

std::vector<Height> DifficultyAdjustment::previousHeightsRequiredForRecalculation(Height height, int epochLength) const {
    std::vector<Height> heights;
    if ((height - 1) % epochLength == 0 && epochLength > 1) {
        for (int i = 0; i <= useLastEpochs; ++i) {
            Height h = (height - 1) - i * epochLength;
            if (h >= 0) {
                heights.push_back(h);
            }
        }
        std::reverse(heights.begin(), heights.end());
    } else if ((height - 1) % epochLength == 0 && height > epochLength * useLastEpochs) {
        for (int i = 0; i <= useLastEpochs; ++i) {
            heights.push_back((height - 1) - i * epochLength);
        }
        std::reverse(heights.begin(), heights.end());
    } else {
        heights.push_back(height - 1);
    }
    return heights;
}

std::vector<Height> DifficultyAdjustment::heightsForNextRecalculation(Height height, int epochLength) const {
    return previousHeightsRequiredForRecalculation(nextRecalculationHeight(height, epochLength), epochLength);
}

difficulty DifficultyAdjustment::bitcoinCalculate(const std::vector<Header>& previousHeaders, int epochLength) const {
    if (previousHeaders.size() < 2) {
        throw std::invalid_argument("At least two headers needed for diff recalculation");
    }
    const Header& start = previousHeaders[previousHeaders.size() - 2];
    const Header& end = previousHeaders.back();
    return bitcoinCalculate(start, end, epochLength);
}

difficulty DifficultyAdjustment::bitcoinCalculate(const Header& start, const Header& end, int epochLength) const {
    return end.requiredDifficulty * desiredInterval * epochLength / (end.timestamp - start.timestamp);
}

difficulty DifficultyAdjustment::eip37Calculate(const std::vector<Header>& previousHeaders, int epochLength) const {
    if (previousHeaders.size() < 2) {
        throw std::invalid_argument("At least two headers needed for diff recalculation");
    }
    difficulty lastDiff = previousHeaders.back().requiredDifficulty;
    difficulty predictiveDiff = calculate(previousHeaders, epochLength);
    difficulty limitedPredictiveDiff = (predictiveDiff > lastDiff)
        ? min(predictiveDiff, lastDiff * 3 / 2)
        : max(predictiveDiff, lastDiff / 2);

    difficulty classicDiff = bitcoinCalculate(previousHeaders, epochLength);
    difficulty avg = (classicDiff + limitedPredictiveDiff) / 2;

    difficulty uncompressedDiff = (avg > lastDiff)
        ? min(avg, lastDiff * 3 / 2)
        : max(avg, lastDiff / 2);

    // Perform serialization cycle to normalize resulted difficulty
    return decodeCompactBits(
        encodeCompactBits(uncompressedDiff)
    );
}

difficulty DifficultyAdjustment::calculate(const std::vector<Header>& previousHeaders, int epochLength) const {
    if (previousHeaders.empty()) {
        throw std::invalid_argument("PreviousHeaders should always contain at least 1 element");
    }

    difficulty uncompressedDiff;
    if (previousHeaders.size() == 1 || previousHeaders.front().timestamp >= previousHeaders.back().timestamp) {
        uncompressedDiff = previousHeaders.front().requiredDifficulty;
    } else {
        std::vector<std::pair<int, difficulty>> data;
        for (size_t i = 0; i < previousHeaders.size() - 1; ++i) {
            const Header& start = previousHeaders[i];
            const Header& end = previousHeaders[i + 1];
            if (end.height - start.height != epochLength) {
                throw std::invalid_argument("Incorrect heights interval for previous headers");
            }
            difficulty diff = end.requiredDifficulty * desiredInterval * epochLength / (end.timestamp - start.timestamp);
            data.emplace_back(end.height, diff);
        }
        difficulty diff = interpolate(data, epochLength);
        uncompressedDiff = (diff >= 1) ? diff : initialDifficulty;
    }

    // Perform serialization cycle to normalize resulted difficulty
    return decodeCompactBits(
        encodeCompactBits(uncompressedDiff)
    );
}

BigInt DifficultyAdjustment::interpolate(const std::vector<std::pair<int, difficulty>>& data, int epochLength) const {
    if (data.size() == 1) {
        return data.front().second;
    }

    BigInt xySum = 0;
    BigInt x2Sum = 0;
    BigInt ySum = 0;
    BigInt xSum = 0;
    size_t size = data.size();

    for (const auto& [x, y] : data) {
        BigInt bigX = x; // Convert x to BigInt
        xySum += bigX * y;
        x2Sum += bigX * bigX;
        ySum += y;
        xSum += bigX;
    }


    BigInt b = (xySum * size - xSum * ySum) * PrecisionConstant / (x2Sum * size - xSum * xSum);
    BigInt a = (ySum * PrecisionConstant - b * xSum) / size / PrecisionConstant;

    int point = data.back().first + epochLength;
    return a + b * point / PrecisionConstant;
}