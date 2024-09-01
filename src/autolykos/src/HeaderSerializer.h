#ifndef HEADERSERIALIZER_H
#define HEADERSERIALIZER_H

#include "includes.h"
#include "serialize.h"

class HeaderSerializer {
public:
    static void serializeWithoutPow(const HeaderWithoutPow& h, Writer& w);
    static std::vector<uint8_t> bytesWithoutPow(const HeaderWithoutPow& header);

    void Serialize(const Header& h, Writer& w);
    std::vector<uint8_t> toBytes(const Header& obj);
};

#endif // HEADERSERIALIZER_H

