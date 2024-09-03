// Copyright (c) 2017-2024 The ERGO developers
// Copyright (c) 2023-2024 The BLOCX developers

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
#include "mining.h"
#include "Writer.h"
#include "AutolykosSolution.h"
#include "Header.h"
#include "HeaderWithoutPow.h"
#include "HeaderSerializer.h"
#include "AutolykosPowScheme.h"
#include "uint256.h"

#endif // INCLUDES_H