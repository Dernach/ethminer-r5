/*
    This file is part of ethminer.

    ethminer is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    ethminer is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with ethminer.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file CommonData.h
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 *
 * Shared algorithms and data types.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>

#include <boost/algorithm/string.hpp>

#include "Common.h"

namespace dev
{
// Improved enums with enum class for type safety
enum class WhenError
{
    DontThrow = 0,
    Throw = 1,
};

enum class HexPrefix
{
    DontAdd = 0,
    Add = 1,
};

enum class ScaleSuffix
{
    DontAdd = 0,
    Add = 1
};

/// Convert a series of bytes to the corresponding string of hex duplets.
/// @param _data data to convert to hex string
/// @param _width specifies the minimum width of the first element
/// @param _prefix whether to add 0x prefix
/// @return hex string representation
template <typename T>
std::string toHex(const T& _data, int _width = 2, HexPrefix _prefix = HexPrefix::DontAdd)
{
    std::ostringstream ret;

    if (_prefix == HexPrefix::Add)
        ret << "0x";

    bool isFirst = true;
    for (auto i : _data)
    {
        ret << std::hex << std::setfill('0') << std::setw(isFirst ? _width : 2)
            << static_cast<int>(typename std::make_unsigned<decltype(i)>::type(i));
        isFirst = false;
    }
    return ret.str();
}

/// Converts a (printable) ASCII hex character into the corresponding integer value.
/// @param _char hex character to convert
/// @param _throwOnError whether to throw on invalid hex chars
/// @return integer value or -1 if invalid and not throwing
int fromHex(char _char, WhenError _throwOnError);

/// Converts a hex string into the corresponding byte stream.
/// @param _hexString hex string to convert
/// @param _throwOnError whether to throw on invalid input
/// @return byte vector with binary representation
bytes fromHex(const std::string& _hexString, WhenError _throwOnError = WhenError::DontThrow);

/// Converts byte array to a string containing the same binary data.
inline std::string asString(const bytes& _bytes)
{
    return std::string(reinterpret_cast<const char*>(_bytes.data()), _bytes.size());
}

/// Converts a string to a byte array containing the string's binary data.
inline bytes asBytes(const std::string& _str)
{
    return bytes(reinterpret_cast<const byte*>(_str.data()),
        reinterpret_cast<const byte*>(_str.data() + _str.size()));
}

// Big-endian conversion functions

/// Converts an integer value to big-endian byte representation
/// @param _value integer value to convert
/// @param _output output container
template <typename T, typename OutContainer>
inline void toBigEndian(T _value, OutContainer& _output)
{
    static_assert(std::is_same<bigint, T>::value || !std::numeric_limits<T>::is_signed,
        "only unsigned types or bigint supported");

    for (auto i = _output.size(); i != 0; _value >>= 8, i--)
    {
        T v = _value & T(0xff);
        _output[i - 1] = static_cast<typename OutContainer::value_type>(static_cast<uint8_t>(v));
    }
}

/// Converts a big-endian byte-stream to an integer value.
/// @param _bytes big-endian byte representation
/// @return integer value
template <typename T, typename Container>
inline T fromBigEndian(const Container& _bytes)
{
    T ret = 0;
    for (auto b : _bytes)
    {
        ret =
            (ret << 8) |
            static_cast<byte>(
                static_cast<typename std::make_unsigned<typename Container::value_type>::type>(b));
    }
    return ret;
}

/// Create a byte array containing the big-endian representation of _value
inline bytes toBigEndian(u256 _value)
{
    bytes ret(32);
    toBigEndian(std::move(_value), ret);
    return ret;
}

/// Create a byte array containing the big-endian representation of _value
inline bytes toBigEndian(u160 _value)
{
    bytes ret(20);
    toBigEndian(_value, ret);
    return ret;
}

/// Create a byte array just big enough to represent _value in big-endian format.
template <typename T>
inline bytes toCompactBigEndian(T _value, unsigned _minBytes = 0)
{
    static_assert(std::is_same<bigint, T>::value || !std::numeric_limits<T>::is_signed,
        "only unsigned types or bigint supported");

    int bytesNeeded = 0;
    for (T v = _value; v; ++bytesNeeded, v >>= 8)
    {
    }

    bytes ret(std::max<unsigned>(_minBytes, bytesNeeded), 0);
    toBigEndian(_value, ret);
    return ret;
}

/// Convert u256 value to hex string
inline std::string toHex(u256 _value, HexPrefix _prefix = HexPrefix::DontAdd)
{
    std::string hex = toHex(toBigEndian(_value));
    return (_prefix == HexPrefix::Add) ? "0x" + hex : hex;
}

/// Convert uint64_t to hex with fixed width
inline std::string toHex(uint64_t _value, HexPrefix _prefix = HexPrefix::DontAdd, int _width = 16)
{
    std::ostringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(_width) << _value;
    return (_prefix == HexPrefix::Add) ? "0x" + ss.str() : ss.str();
}

/// Convert uint32_t to hex with fixed width
inline std::string toHex(uint32_t _value, HexPrefix _prefix = HexPrefix::DontAdd, int _width = 8)
{
    std::ostringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(_width) << _value;
    return (_prefix == HexPrefix::Add) ? "0x" + ss.str() : ss.str();
}

/// Convert uint64_t to compact hex (no leading zeros)
inline std::string toCompactHex(uint64_t _value, HexPrefix _prefix = HexPrefix::DontAdd)
{
    std::ostringstream ss;
    ss << std::hex << _value;
    return (_prefix == HexPrefix::Add) ? "0x" + ss.str() : ss.str();
}

/// Convert uint32_t to compact hex (no leading zeros)
inline std::string toCompactHex(uint32_t _value, HexPrefix _prefix = HexPrefix::DontAdd)
{
    std::ostringstream ss;
    ss << std::hex << _value;
    return (_prefix == HexPrefix::Add) ? "0x" + ss.str() : ss.str();
}

/// Escapes a string into the C-string representation.
/// @param _str string to escape
/// @param _escapeAll if true will escape all characters, not just unprintable ones
std::string escaped(const std::string& _str, bool _escapeAll = true);

/// Determine bytes required to encode the given integer value.
/// @returns 0 if @a _value is zero.
template <typename T>
inline unsigned bytesRequired(T _value)
{
    static_assert(std::is_same<bigint, T>::value || !std::numeric_limits<T>::is_signed,
        "only unsigned types or bigint supported");

    unsigned count = 0;
    for (; _value != 0; ++count, _value >>= 8)
    {
    }
    return count;
}

/// Sets environment variable in a cross-platform way.
/// @param name environment variable name
/// @param value environment variable value
/// @param override whether to override if already set
/// @return success or failure
bool setenv(const char name[], const char value[], bool override = false);

/// Calculate target hash from difficulty
/// @param diff mining difficulty
/// @param _prefix whether to add 0x prefix
/// @return target hash as hex string
std::string getTargetFromDiff(double diff, HexPrefix _prefix = HexPrefix::Add);

/// Calculate required hashes for a target
/// @param _target target hash as hex string
/// @return number of hashes required on average
double getHashesToTarget(const std::string _target);

/// Format a value with appropriate scale and suffix
/// @param _value value to format
/// @param _divisor divisor for scaling (e.g., 1000 or 1024)
/// @param _precision decimal places to show
/// @param _sizes array of suffix strings
/// @param _numsizes number of suffixes available
/// @param _suffix whether to add the suffix
/// @return formatted string
std::string getScaledSize(double _value, double _divisor, int _precision,
    const std::string _sizes[], size_t _numsizes, ScaleSuffix _suffix = ScaleSuffix::Add);

/// Format hashrate with appropriate units (h, Kh, Mh, Gh)
/// @param _hr hashrate to format
/// @param _suffix whether to add unit suffix
/// @param _precision decimal places to show
/// @return formatted hashrate string
std::string getFormattedHashes(
    double _hr, ScaleSuffix _suffix = ScaleSuffix::Add, int _precision = 2);

/// Format memory size with appropriate units (B, KB, MB, GB)
/// @param _mem memory size to format
/// @param _suffix whether to add unit suffix
/// @param _precision decimal places to show
/// @return formatted memory size string
std::string getFormattedMemory(
    double _mem, ScaleSuffix _suffix = ScaleSuffix::Add, int _precision = 2);

/// Add padding characters to the left of a string to reach specified length
/// @param _value original string
/// @param _length desired length
/// @param _fillChar character to use for padding
/// @return padded string
std::string padLeft(const std::string& _value, size_t _length, char _fillChar);

/// Add padding characters to the right of a string to reach specified length
/// @param _value original string
/// @param _length desired length
/// @param _fillChar character to use for padding
/// @return padded string
std::string padRight(const std::string& _value, size_t _length, char _fillChar);

}  // namespace dev
