#ifndef CORE_H
#define CORE_H

#include "includes.h"

using ModifierId = std::vector<uint8_t>;

std::vector<uint8_t> idToBytes(const ModifierId& id);
ModifierId bytesToId(const std::vector<uint8_t>& transformedId);

#endif // CORE_H

