#ifndef ERGONODEVIEWMODIFIER_H
#define ERGONODEVIEWMODIFIER_H

#include "includes.h"

class ErgoNodeViewModifier {
public:
    std::vector<uint8_t> bytes(const Header& header);
    std::vector<uint8_t> serializedId(const Header& header);
    ModifierId id(const Header& header); // Method to get the id

    static const int HashLength = 32;
    static const int EmptyHistoryHeight = 0;
    static const int GenesisHeight = EmptyHistoryHeight + 1; // first block has height == 1
    static ModifierId GenesisParentId();
};

#endif // ERGONODEVIEWMODIFIER_H


