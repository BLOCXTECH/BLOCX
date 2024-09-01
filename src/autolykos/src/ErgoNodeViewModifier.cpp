#include "includes.h"

std::vector<uint8_t> ErgoNodeViewModifier::bytes(const Header& header) {
    HeaderSerializer serializer;
    return serializer.toBytes(header);
}

std::vector<uint8_t> ErgoNodeViewModifier::serializedId(const Header& header) {
    return hash(bytes(header));
}

ModifierId ErgoNodeViewModifier::id(const Header& header) {
    return bytesToId(serializedId(header));
}

ModifierId ErgoNodeViewModifier::GenesisParentId() {
    static ModifierId genesisParentId = bytesToId(std::vector<uint8_t>(HashLength, 0));
    return genesisParentId;
}
