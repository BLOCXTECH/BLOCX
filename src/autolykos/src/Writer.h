#ifndef WRITER_H
#define WRITER_H

#include "includes.h"

class Writer {
public:
    std::vector<uint8_t> buffer;

    void put(uint8_t value);
    void putBytes(const std::vector<uint8_t>& bytes);
    void putBytes(const uint8_t* bytes, size_t length);
    void putULong(uint64_t value);
    void putUInt(uint32_t value);
    void putUByte(uint8_t value);
    void write(const char* data, size_t size) {
        buffer.insert(buffer.end(), data, data + size);
    }

    template <typename T>
    void write(const T& value) {
        write(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    std::vector<uint8_t> result() const;
};

#endif // WRITER_H

