#ifndef INCLUDES_H
#define INCLUDES_H

// Include all necessary header files here
#include <iostream>
#include <vector>
#include <string>

#include <algorithm> // for std::min
#include <boost/algorithm/hex.hpp>
#include <boost/endian/conversion.hpp>
#include <cmath> // for std::pow
#include <sstream> // for std::ostringstream
#include <iomanip> // for std::hex and std::setfill
#include <optional>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/multiprecision/cpp_bin_float.hpp>
#include <cstdint>
#include <stdexcept>
#include <secp256k1.h>
#include <cstring>
#include <array>
#include <logging.h>
#include <boost/uuid/detail/md5.hpp>
#include <random>
#include <climits>

#include "arith_uint256.h"
#include "blake2/blake2b.h"
#include "NumericHash.h"
#include "mining.h"
#include "Writer.h"
#include "AutolykosSolution.h"
#include "DifficultySerializer.h"
#include "Header.h"
#include "HeaderWithoutPow.h"
#include "CryptoFacade.h"
#include "core.h"
#include "HeaderSerializer.h"
#include "ErgoNodeViewModifier.h"
#include "AutolykosPowScheme.h"
#include "ChainSettings.h"
#include "DifficultyAdjustment.h"
#include "uint256.h"

#endif // INCLUDES_H