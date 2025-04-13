// ethminer -- Ethereum miner with OpenCL, CUDA and stratum support.
// Copyright 2018-2023 ethminer Authors.
// Licensed under GNU General Public License, Version 3. See the LICENSE file.

/// @file
/// Common types and definitions used throughout the codebase.

#pragma once

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <boost/multiprecision/cpp_int.hpp>

#include "vector_ref.h"

// Use uint8_t instead of custom byte type to avoid confusion
using byte = std::uint8_t;

namespace dev
{
// Binary data types - prefer vector_ref for improved performance with large data
using bytes = std::vector<byte>;
using bytesRef = vector_ref<byte>;
using bytesConstRef = vector_ref<const byte>;

// Numeric types - using more descriptive namespace alias for boost multiprecision
namespace mp = boost::multiprecision;

// Clearly define numeric types with consistent naming
using bigint = mp::number<mp::cpp_int_backend<>>;

template <unsigned N>
using uint_t = mp::number<mp::cpp_int_backend<N, N, mp::unsigned_magnitude, mp::unchecked, void>>;

using u64 = uint_t<64>;
using u128 = uint_t<128>;
using u160 = uint_t<160>;
using u256 = uint_t<256>;
using u512 = uint_t<512>;

// Null/Invalid values
const u256 Invalid256 = ~u256(0);

/// Converts arbitrary value to string representation using std::stringstream.
template <typename T>
inline std::string toString(const T& _value)
{
    std::ostringstream stream;
    stream << _value;
    return stream.str();
}

}  // namespace dev
