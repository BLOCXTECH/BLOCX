#include "includes.h"

using namespace boost::multiprecision;

std::vector<uint8_t> uint32ToByteArrayBE(uint64_t value) {
    std::vector<uint8_t> bytes(4);
    bytes[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
    bytes[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    bytes[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    bytes[3] = static_cast<uint8_t>(value & 0xFF);
    return bytes;
}

void serialize(uint32_t obj, Writer& w) {
    w.putBytes(uint32ToByteArrayBE(obj));
}

/**
  * Binary serializer and (en/de)coding utils for difficulty presented in different formats (as a big integer or nbits
  * encoded 32 bits number)
  */

uint64_t encodeCompactBits(const cpp_int& requiredDifficulty) {
    std::vector<uint8_t> value;
    export_bits(requiredDifficulty, std::back_inserter(value), 8);

    int size = value.size();
    uint64_t result = 0;

    if (size <= 3) {
        cpp_int adjustedDifficulty = requiredDifficulty << (8 * (3 - size));
        result = adjustedDifficulty.convert_to<uint64_t>();
    } else {
        cpp_int adjustedDifficulty = requiredDifficulty >> (8 * (size - 3));
        result = adjustedDifficulty.convert_to<uint64_t>();
    }

    // The 0x00800000 bit denotes the sign.
    // Thus, if it is already set, divide the mantissa by 256 and increase the exponent.
    if ((result & 0x00800000L) != 0) {
        result >>= 8;
        size += 1;
    }

    result |= static_cast<uint64_t>(size) << 24;

    if (requiredDifficulty < 0) {
        result |= 0x00800000;
    }

    return result;
}

/** Parse 4 bytes from the byte array (starting at the offset) as unsigned 32-bit integer in big endian format. */
uint32_t readUint32BE(const std::vector<uint8_t>& bytes) {
    return boost::endian::load_big_u32(bytes.data());
}

/**
  * MPI encoded numbers are produced by the OpenSSL BN_bn2mpi function. They consist of
  * a 4 byte big endian length field, followed by the stated number of bytes representing
  * the number in big endian format (with a sign bit).
  *
  * @param hasLength can be set to false if the given array is missing the 4 byte length field
  */
cpp_int decodeMPI(const std::vector<uint8_t>& mpi, bool hasLength) {
    std::vector<uint8_t> buf;
    if (hasLength) {
        int length = readUint32BE(mpi);
        buf.insert(buf.end(), mpi.begin() + 4, mpi.begin() + 4 + length);
    } else {
        buf = mpi;
    }

    if (buf.empty()) {
        return cpp_int(0);
    }

    bool isNegative = (buf[0] & 0x80) == 0x80;
    if (isNegative) {
        buf[0] &= 0x7f;
    }

    cpp_int result;
    import_bits(result, buf.begin(), buf.end());
    if (isNegative) {
        result = -result;
    }

    return result;
}

/**
  * <p>The "compact" format is a representation of a whole number N using an unsigned 32 bit number similar to a
  * floating point format. The most significant 8 bits are the unsigned exponent of base 256. This exponent can
  * be thought of as "number of bytes of N". The lower 23 bits are the mantissa. Bit number 24 (0x800000) represents
  * the sign of N. Therefore, N = (-1^sign) * mantissa * 256^(exponent-3).</p>
  *
  * <p>Satoshi's original implementation used BN_bn2mpi() and BN_mpi2bn(). MPI uses the most significant bit of the
  * first byte as sign. Thus 0x1234560000 is compact 0x05123456 and 0xc0de000000 is compact 0x0600c0de. Compact
  * 0x05c0de00 would be -0x40de000000.</p>
  *
  * <p>Bitcoin only uses this "compact" format for encoding difficulty targets, which are unsigned 256bit quantities.
  * Thus, all the complexities of the sign bit and using base 256 are probably an implementation accident.</p>
  */
cpp_int decodeCompactBits(uint32_t compact) {
    int size = (compact >> 24) & 0xFF;
    std::vector<uint8_t> bytes(4 + size, 0);
    bytes[3] = static_cast<uint8_t>(size);
    if (size >= 1) bytes[4] = static_cast<uint8_t>((compact >> 16) & 0xFF);
    if (size >= 2) bytes[5] = static_cast<uint8_t>((compact >> 8) & 0xFF);
    if (size >= 3) bytes[6] = static_cast<uint8_t>(compact & 0xFF);

    return decodeMPI(bytes, true);
}
