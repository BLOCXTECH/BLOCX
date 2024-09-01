#include "includes.h"

using ModifierId = std::vector<uint8_t>;

std::vector<uint8_t> idToBytes(const ModifierId& id) {
    // Hypothetical transformation: combine each pair of bytes into one byte
    std::vector<uint8_t> transformedId;
    for (size_t i = 0; i < id.size(); i += 2) {
        if (i + 1 < id.size()) {
            transformedId.push_back((id[i] << 4) | (id[i + 1] & 0x0F));
        } else {
            transformedId.push_back(id[i]);
        }
    }
    return transformedId;
}

ModifierId bytesToId(const std::vector<uint8_t>& transformedId) {
    ModifierId originalId;
    for (size_t i = 0; i < transformedId.size(); ++i) {
        // Extract the high 4 bits for the first byte
        char highNibble = (transformedId[i] >> 4) & 0x0F;
        // Extract the low 4 bits for the second byte, if it exists
        char lowNibble = transformedId[i] & 0x0F;
        
        originalId.push_back(highNibble);
        originalId.push_back(lowNibble);
    }
    return originalId;
}
