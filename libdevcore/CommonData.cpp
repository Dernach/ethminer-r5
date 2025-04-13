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

#include <array>
#include <cstdlib>
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

#include "CommonData.h"
#include "Exceptions.h"

using namespace std;
using namespace dev;

/**
 * Converts a single hexadecimal character to its integer value.
 * 
 * @param _char Character to convert
 * @param _throwOnError Whether to throw an exception on invalid input
 * @return Integer value of the hex character, or -1 if invalid and not throwing
 */
int dev::fromHex(char _char, WhenError _throwOnError)
{
    // Handle decimal digits
    if (_char >= '0' && _char <= '9')
        return _char - '0';
    
    // Handle lowercase hex
    if (_char >= 'a' && _char <= 'f')
        return _char - 'a' + 10;
    
    // Handle uppercase hex
    if (_char >= 'A' && _char <= 'F')
        return _char - 'A' + 10;

    // Invalid character
    if (_throwOnError == WhenError::Throw)
        BOOST_THROW_EXCEPTION(BadHexCharacter() << errinfo_invalidSymbol(_char));

    return -1;
}

/**
 * Converts a hexadecimal string to bytes.
 * 
 * @param _str Hexadecimal string, with optional '0x' prefix
 * @param _throwOnError Whether to throw an exception on invalid input
 * @return Byte array containing the converted values
 */
bytes dev::fromHex(const std::string& _str, WhenError _throwOnError)
{
    if (_str.empty())
        return bytes();

    size_t startPos = 0;

    // Skip '0x' prefix if present
    if (_str.size() >= 2 && _str[0] == '0' && (_str[1] == 'x' || _str[1] == 'X'))
        startPos = 2;

    // Estimate the result size to avoid reallocations
    bytes result;
    result.reserve((_str.size() - startPos + 1) / 2);

    // Process odd-length string separately
    if ((_str.size() - startPos) % 2)
    {
        int h = fromHex(_str[startPos], WhenError::DontThrow);
        if (h != -1)
            result.push_back(static_cast<byte>(h));
        else if (_throwOnError == WhenError::Throw)
            BOOST_THROW_EXCEPTION(BadHexCharacter());
        else
            return bytes();
        startPos++;
    }

    // Process hex pairs
    for (size_t i = startPos; i < _str.size(); i += 2)
    {
        int high = fromHex(_str[i], WhenError::DontThrow);
        int low = fromHex(_str[i + 1], WhenError::DontThrow);

        if (high != -1 && low != -1)
            result.push_back(static_cast<byte>((high << 4) | low)); // Use bitwise OR for clarity
        else if (_throwOnError == WhenError::Throw)
            BOOST_THROW_EXCEPTION(BadHexCharacter());
        else
            return bytes();
    }

    return result;
}

/**
 * Sets an environment variable in a cross-platform manner.
 * 
 * @param name The name of the environment variable
 * @param value The value to set
 * @param override Whether to override existing value if present
 * @return True if successful, false otherwise
 */
bool dev::setenv(const char name[], const char value[], bool override)
{
#if _WIN32
    if (!override && std::getenv(name) != nullptr)
        return true;

    return ::_putenv_s(name, value) == 0;
#else
    return ::setenv(name, value, override ? 1 : 0) == 0;
#endif
}

/**
 * Calculates a target hash from a difficulty value.
 * 
 * @param diff The difficulty value
 * @param _prefix Whether to add "0x" prefix to the result
 * @return Target hash as a hexadecimal string
 */
std::string dev::getTargetFromDiff(double diff, HexPrefix _prefix)
{
    using namespace boost::multiprecision;
    using BigInteger = cpp_int;

    // Constants
    static const BigInteger maxValue(
        "0xffff000000000000000000000000000000000000000000000000000000000000");
    static const BigInteger pow2_32("0x100000000");

    // Round the difficulty to 6 decimal places to avoid rounding errors
    // Multiply by 1000000, then divide
    BigInteger difficulty_scaled = BigInteger(round(diff * 1000000));
    BigInteger multiplier = difficulty_scaled * pow2_32 / BigInteger(1000000);

    // Avoid division by zero
    BigInteger product = (multiplier > 0) ? maxValue / multiplier : maxValue;

    // Normalize to 64 chars hex
    std::stringstream ss;
    ss << (_prefix == HexPrefix::Add ? "0x" : "") 
       << std::hex << std::setw(64) << std::setfill('0') << product;

    std::string target = ss.str();
    boost::algorithm::to_lower(target);
    return target;
}

/**
 * Calculates the number of hashes needed to find a block with the given target.
 * 
 * @param _target The target hash as a hexadecimal string
 * @return The estimated number of hashes required
 */
double dev::getHashesToTarget(string _target)
{
    using namespace boost::multiprecision;
    using BigInteger = boost::multiprecision::cpp_int;

    static const BigInteger dividend(
        "0xffff000000000000000000000000000000000000000000000000000000000000");
    
    BigInteger divisor(_target);
    return static_cast<double>(dividend / divisor);
}

/**
 * Scales a numeric value and formats it with appropriate units.
 * 
 * @param _value The value to scale
 * @param _divisor The divisor for each scale unit (e.g., 1000 or 1024)
 * @param _precision The number of decimal places to display
 * @param _sizes Array of unit strings (e.g., "B", "KB", "MB")
 * @param _numsizes Number of entries in the _sizes array
 * @param _suffix Whether to append the unit suffix to the result
 * @return Formatted string with scaled value and optional unit
 */
std::string dev::getScaledSize(double _value, double _divisor, int _precision,
    const std::string _sizes[], size_t _numsizes, ScaleSuffix _suffix)
{
    if (_value < 0 || _divisor <= 0 || _numsizes == 0)
        return "0";
        
    double scaledValue = _value;
    size_t unitIndex = 0;

    // Find appropriate scale
    while (scaledValue >= _divisor && unitIndex < (_numsizes - 1))
    {
        scaledValue /= _divisor;
        unitIndex++;
    }

    // Format the result
    std::ostringstream formatter;
    formatter << std::fixed << std::setprecision(_precision) << scaledValue;

    if (_suffix == ScaleSuffix::Add && unitIndex < _numsizes)
        formatter << " " << _sizes[unitIndex];

    return formatter.str();
}

/**
 * Formats a hash rate with appropriate units (h, Kh, Mh, Gh).
 * 
 * @param _hr Hash rate in hashes per second
 * @param _suffix Whether to append the unit suffix to the result
 * @param _precision The number of decimal places to display
 * @return Formatted string with scaled hash rate
 */
std::string dev::getFormattedHashes(double _hr, ScaleSuffix _suffix, int _precision)
{
    static const std::string suffixes[] = {"h", "Kh", "Mh", "Gh"};
    return getScaledSize(_hr, 1000.0, _precision, suffixes, 4, _suffix);
}

/**
 * Formats a memory size with appropriate units (B, KB, MB, GB).
 * 
 * @param _mem Memory size in bytes
 * @param _suffix Whether to append the unit suffix to the result
 * @param _precision The number of decimal places to display
 * @return Formatted string with scaled memory size
 */
std::string dev::getFormattedMemory(double _mem, ScaleSuffix _suffix, int _precision)
{
    static const std::string suffixes[] = {"B", "KB", "MB", "GB"};
    return getScaledSize(_mem, 1024.0, _precision, suffixes, 4, _suffix);
}

/**
 * Pads a string on the left with a specified character.
 * 
 * @param _value The string to pad
 * @param _length The desired total length
 * @param _fillChar The character to use for padding
 * @return Padded string
 */
std::string dev::padLeft(const std::string& _value, size_t _length, char _fillChar)
{
    if (_length <= _value.size())
        return _value;

    return std::string(_length - _value.size(), _fillChar) + _value;
}

/**
 * Pads a string on the right with a specified character.
 * 
 * @param _value The string to pad
 * @param _length The desired total length
 * @param _fillChar The character to use for padding
 * @return Padded string
 */
std::string dev::padRight(const std::string& _value, size_t _length, char _fillChar)
{
    if (_length <= _value.size())
        return _value;

    std::string result(_value);
    result.resize(_length, _fillChar);
    return result;
}
