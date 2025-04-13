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
/** @file FixedHash.h
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 *
 * The FixedHash fixed-size "hash" container type.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <random>

#include "CommonData.h"

namespace dev
{

// Global random number generator for fixed hash functions
extern std::random_device s_fixedHashEngine;

/**
 * @brief Fixed-size raw-byte array container type, optimized for storing hashes.
 *
 * Transparently converts to/from the corresponding arithmetic type, assuming
 * the data contained in the hash is big-endian.
 *
 * @tparam N Size of the hash in bytes
 */
template <unsigned N>
class FixedHash
{
public:
    // Platform-specific ellipsis symbol for abridged representation
#if defined(_WIN32)
    static constexpr const char* k_ellipsis = "...";
#else
    static constexpr const char* k_ellipsis = "\342\200\246";
#endif

    /// The corresponding arithmetic type for this hash size
    using Arith = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<N * 8, N * 8,
        boost::multiprecision::unsigned_magnitude, boost::multiprecision::unchecked, void>>;

    /// The size of the container in bytes
    static constexpr unsigned size = N;

    /// Flag to avoid accidental construction from pointer
    enum ConstructFromPointerType
    {
        ConstructFromPointer
    };

    /// Method to convert from a string
    enum ConstructFromHashType
    {
        AlignLeft,       ///< Align the input data to the left (pad right with zeros)
        AlignRight,      ///< Align the input data to the right (pad left with zeros)
        FailIfDifferent  ///< Fail construction if sizes don't match
    };

    /**
     * @brief Construct an empty hash (all zeros)
     */
    FixedHash() { m_data.fill(0); }

    /**
     * @brief Construct from another hash, filling with zeroes or cropping as necessary
     *
     * @tparam M Size of the source hash
     * @param _h Source hash to copy from
     * @param _t Alignment type for the copy operation
     */
    template <unsigned M>
    explicit FixedHash(FixedHash<M> const& _h, ConstructFromHashType _t = AlignLeft)
    {
        m_data.fill(0);
        unsigned c = std::min(M, N);
        for (unsigned i = 0; i < c; ++i)
            m_data[_t == AlignRight ? N - 1 - i : i] = _h[_t == AlignRight ? M - 1 - i : i];
    }

    /**
     * @brief Convert from the corresponding arithmetic type
     *
     * @param _arith Arithmetic value to convert
     */
    explicit FixedHash(Arith const& _arith) { toBigEndian(_arith, m_data); }

    /**
     * @brief Convert from unsigned integer
     *
     * @param _u Unsigned integer to convert
     */
    explicit FixedHash(unsigned _u) { toBigEndian(_u, m_data); }

    /**
     * @brief Explicitly construct, copying from a byte array
     *
     * @param _b Byte array to copy from
     * @param _t Hash construction type (alignment or fail behavior)
     */
    explicit FixedHash(bytes const& _b, ConstructFromHashType _t = FailIfDifferent)
    {
        if (_b.size() == N)
            std::memcpy(m_data.data(), _b.data(), std::min<unsigned>(_b.size(), N));
        else
        {
            m_data.fill(0);
            if (_t != FailIfDifferent)
            {
                auto c = std::min<unsigned>(_b.size(), N);
                for (unsigned i = 0; i < c; ++i)
                    m_data[_t == AlignRight ? N - 1 - i : i] =
                        _b[_t == AlignRight ? _b.size() - 1 - i : i];
            }
        }
    }

    /**
     * @brief Explicitly construct, copying from a byte array reference
     *
     * @param _b Byte array reference to copy from
     * @param _t Hash construction type (alignment or fail behavior)
     */
    explicit FixedHash(bytesConstRef _b, ConstructFromHashType _t = FailIfDifferent)
    {
        if (_b.size() == N)
            std::memcpy(m_data.data(), _b.data(), std::min<unsigned>(_b.size(), N));
        else
        {
            m_data.fill(0);
            if (_t != FailIfDifferent)
            {
                auto c = std::min<unsigned>(_b.size(), N);
                for (unsigned i = 0; i < c; ++i)
                    m_data[_t == AlignRight ? N - 1 - i : i] =
                        _b[_t == AlignRight ? _b.size() - 1 - i : i];
            }
        }
    }

    /**
     * @brief Explicitly construct, copying from bytes in memory
     *
     * @param _bs Pointer to byte array
     * @param _ Construction from pointer flag (unused)
     */
    explicit FixedHash(byte const* _bs, ConstructFromPointerType /*unused*/)
    {
        std::memcpy(m_data.data(), _bs, N);
    }

    /**
     * @brief Construct from a hex string
     *
     * @param _s Hex string to parse
     */
    explicit FixedHash(std::string const& _s)
      : FixedHash(fromHex(_s, WhenError::Throw), FailIfDifferent)
    {}

    /**
     * @brief Convert to arithmetic type
     *
     * @return Arith The arithmetic representation
     */
    operator Arith() const { return fromBigEndian<Arith>(m_data); }

    /**
     * @brief Check if this is a non-zero hash
     *
     * @return true if any byte is non-zero
     * @return false if all bytes are zero
     */
    explicit operator bool() const
    {
        return std::any_of(m_data.begin(), m_data.end(), [](byte _b) { return _b != 0; });
    }

    // Comparison operators
    bool operator==(FixedHash const& _c) const { return m_data == _c.m_data; }
    bool operator!=(FixedHash const& _c) const { return m_data != _c.m_data; }
    bool operator<(FixedHash const& _c) const
    {
        for (unsigned i = 0; i < N; ++i)
        {
            if (m_data[i] < _c.m_data[i])
                return true;
            if (m_data[i] > _c.m_data[i])
                return false;
        }
        return false;
    }
    bool operator>=(FixedHash const& _c) const { return !operator<(_c); }
    bool operator<=(FixedHash const& _c) const { return operator==(_c) || operator<(_c); }
    bool operator>(FixedHash const& _c) const { return !operator<=(_c); }

    // Bitwise operations
    FixedHash& operator^=(FixedHash const& _c)
    {
        for (unsigned i = 0; i < N; ++i)
            m_data[i] ^= _c.m_data[i];
        return *this;
    }
    FixedHash operator^(FixedHash const& _c) const { return FixedHash(*this) ^= _c; }

    FixedHash& operator|=(FixedHash const& _c)
    {
        for (unsigned i = 0; i < N; ++i)
            m_data[i] |= _c.m_data[i];
        return *this;
    }
    FixedHash operator|(FixedHash const& _c) const { return FixedHash(*this) |= _c; }

    FixedHash& operator&=(FixedHash const& _c)
    {
        for (unsigned i = 0; i < N; ++i)
            m_data[i] &= _c.m_data[i];
        return *this;
    }
    FixedHash operator&(FixedHash const& _c) const { return FixedHash(*this) &= _c; }

    FixedHash operator~() const
    {
        FixedHash ret;
        for (unsigned i = 0; i < N; ++i)
            ret[i] = ~m_data[i];
        return ret;
    }

    /**
     * @brief Big-endian increment
     *
     * @return FixedHash& Reference to self after increment
     */
    FixedHash& operator++()
    {
        for (unsigned i = size; i > 0 && !++m_data[--i];)
        {
            // Keep incrementing until we find a position where we don't overflow
        }
        return *this;
    }

    /**
     * @brief Access a particular byte from the hash
     *
     * @param _i Index of the byte
     * @return byte& Reference to the byte
     */
    byte& operator[](unsigned _i) { return m_data[_i]; }

    /**
     * @brief Access a particular byte from the hash (const version)
     *
     * @param _i Index of the byte
     * @return byte The byte value
     */
    byte operator[](unsigned _i) const { return m_data[_i]; }

    /**
     * @brief Get an abridged version of the hash as a user-readable hex string
     *
     * @return std::string Abridged hex representation
     */
    std::string abridged() const { return toHex(ref().cropped(0, 4)) + k_ellipsis; }

    /**
     * @brief Get the hash as a user-readable hex string
     *
     * @param _prefix Whether to add 0x prefix
     * @return std::string Complete hex representation
     */
    std::string hex(HexPrefix _prefix = HexPrefix::DontAdd) const
    {
        return toHex(ref(), 2, _prefix);
    }

    /**
     * @brief Get a mutable byte reference to the object's data
     *
     * @return bytesRef Mutable reference
     */
    bytesRef ref() { return bytesRef(m_data.data(), N); }

    /**
     * @brief Get a constant byte reference to the object's data
     *
     * @return bytesConstRef Constant reference
     */
    bytesConstRef ref() const { return bytesConstRef(m_data.data(), N); }

    /**
     * @brief Get a mutable byte pointer to the object's data
     *
     * @return byte* Pointer to data
     */
    byte* data() { return m_data.data(); }

    /**
     * @brief Get a constant byte pointer to the object's data
     *
     * @return byte const* Constant pointer to data
     */
    byte const* data() const { return m_data.data(); }

    /**
     * @brief Populate with random data
     *
     * @tparam Engine Random number engine type
     * @param _eng Random number engine instance
     */
    template <class Engine>
    void randomize(Engine& _eng)
    {
        std::uniform_int_distribution<uint16_t> dist(0, 255);
        for (auto& i : m_data)
            i = static_cast<uint8_t>(dist(_eng));
    }

    /**
     * @brief Create a hash with random content
     *
     * @return FixedHash Random hash
     */
    static FixedHash random()
    {
        FixedHash ret;
        ret.randomize(s_fixedHashEngine);
        return ret;
    }

    /**
     * @brief Hash functor for use in containers like unordered_map
     */
    struct hash
    {
        /**
         * @brief Create a hash of the object's data
         *
         * @param _value Hash to process
         * @return size_t Hash value
         */
        size_t operator()(FixedHash const& _value) const
        {
            return boost::hash_range(_value.m_data.cbegin(), _value.m_data.cend());
        }
    };

    /**
     * @brief Reset the hash to all zeros
     */
    void clear() { m_data.fill(0); }

private:
    std::array<byte, N> m_data;  ///< The binary data.
};

/**
 * @brief Fast equality operator for h256
 */
template <>
inline bool FixedHash<32>::operator==(FixedHash<32> const& _other) const
{
    const uint64_t* hash1 = reinterpret_cast<const uint64_t*>(data());
    const uint64_t* hash2 = reinterpret_cast<const uint64_t*>(_other.data());
    return (hash1[0] == hash2[0]) && (hash1[1] == hash2[1]) && (hash1[2] == hash2[2]) &&
           (hash1[3] == hash2[3]);
}

/**
 * @brief Fast std::hash compatible hash function for h256
 */
template <>
inline size_t FixedHash<32>::hash::operator()(FixedHash<32> const& value) const
{
    uint64_t const* data = reinterpret_cast<uint64_t const*>(value.data());
    return boost::hash_range(data, data + 4);
}

/**
 * @brief Stream output operator for FixedHash
 *
 * @tparam N Size of the hash
 * @param _out Output stream
 * @param _h Hash to output
 * @return std::ostream& Reference to the output stream
 */
template <unsigned N>
inline std::ostream& operator<<(std::ostream& _out, FixedHash<N> const& _h)
{
    _out << std::noshowbase << std::hex << std::setfill('0');
    for (unsigned i = 0; i < N; ++i)
        _out << std::setw(2) << static_cast<int>(_h[i]);
    _out << std::dec;
    return _out;
}

// Common types of FixedHash
using h2048 = FixedHash<256>;
using h1024 = FixedHash<128>;
using h520 = FixedHash<65>;
using h512 = FixedHash<64>;
using h256 = FixedHash<32>;
using h160 = FixedHash<20>;
using h128 = FixedHash<16>;
using h64 = FixedHash<8>;

// Vector types of common hashes
using h512s = std::vector<h512>;
using h256s = std::vector<h256>;
using h160s = std::vector<h160>;

/**
 * @brief Convert a vector of h256 to string
 *
 * @param _bs Vector of h256
 * @return std::string String representation
 */
inline std::string toString(h256s const& _bs)
{
    std::ostringstream out;
    out << "[ ";
    for (auto i : _bs)
        out << i.abridged() << ", ";
    out << "]";
    return out.str();
}

}  // namespace dev

namespace std
{
/// Forward std::hash<dev::FixedHash> to dev::FixedHash::hash

template <>
struct hash<dev::h64> : dev::h64::hash
{
};

template <>
struct hash<dev::h128> : dev::h128::hash
{
};

template <>
struct hash<dev::h160> : dev::h160::hash
{
};

template <>
struct hash<dev::h256> : dev::h256::hash
{
};

template <>
struct hash<dev::h512> : dev::h512::hash
{
};

}  // namespace std
