#include "includes.h"

void Writer::put(uint8_t value) {
    buffer.push_back(value);
}

void Writer::putBytes(const std::vector<uint8_t>& bytes) {
    buffer.insert(buffer.end(), bytes.begin(), bytes.end());
}

void Writer::putBytes(const uint8_t* bytes, size_t length) {
    buffer.insert(buffer.end(), bytes, bytes + length);
}

void Writer::putULong(uint64_t value) {
    while (value >= 0x80) {
        buffer.push_back(static_cast<uint8_t>(value | 0x80));
        value >>= 7;
    }
    buffer.push_back(static_cast<uint8_t>(value));
}
void Writer::putUInt(uint32_t value) {
    while (value >= 0x80) {
        buffer.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    buffer.push_back(static_cast<uint8_t>(value));
}

void Writer::putUByte(uint8_t value) {
    buffer.push_back(value);
}

std::vector<uint8_t> Writer::result() const {
    return buffer;
}

