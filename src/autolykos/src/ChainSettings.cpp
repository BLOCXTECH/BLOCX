#include "includes.h"

ChainSettings::ChainSettings(FiniteDuration blockInterval, int useLastEpochs, int epochLength, const std::string& initialDifficultyHex)
    : blockInterval(blockInterval), useLastEpochs(useLastEpochs), epochLength(epochLength), initialDifficultyHex(initialDifficultyHex) {
    initialDifficulty = decodeHexToBigInt(initialDifficultyHex);
}

cpp_int ChainSettings::decodeHexToBigInt(const std::string& hex) const {
    try {
        cpp_int value(hex); // cpp_int can directly parse hex string if prefixed with "0x"
        return value;
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse initialDifficultyHex: " + hex);
    }
}