#pragma once

#include <cassert>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace dev
{

/**
 * @brief A modifiable reference to an existing object or vector in memory.
 *
 * This template class provides a lightweight view into existing memory,
 * similar to std::span in C++20 but with additional functionality.
 * It doesn't own the memory it points to, but provides convenient accessors
 * and operations.
 *
 * @tparam _T The type of data being referenced
 */
template <class _T>
class vector_ref
{
public:
    using value_type = _T;
    using element_type = _T;
    using mutable_value_type = typename std::conditional<std::is_const<_T>::value,
        typename std::remove_const<_T>::type, _T>::type;

    // Ensure we only operate on POD types for safe memory operations
    static_assert(std::is_pod<value_type>::value,
        "vector_ref can only be used with PODs due to its low-level treatment of data.");

    /**
     * @brief Default constructor creates an empty reference
     */
    vector_ref() : m_data(nullptr), m_count(0) {}

    /**
     * @brief Creates a reference to @a _count elements starting at @a _data.
     * @param _data Pointer to the start of the data
     * @param _count Number of elements in the referenced range
     */
    vector_ref(_T* _data, size_t _count) : m_data(_data), m_count(_count) {}

    /**
     * @brief Creates a reference to the data in a string (pointer version)
     * @param _data Pointer to the string containing the data
     */
    vector_ref(
        typename std::conditional<std::is_const<_T>::value, std::string const*, std::string*>::type
            _data)
      : m_data(reinterpret_cast<_T*>(_data->data())), m_count(_data->size() / sizeof(_T))
    {}

    /**
     * @brief Creates a reference to the data in a vector (pointer version)
     * @param _data Pointer to the vector containing the data
     */
    vector_ref(typename std::conditional<std::is_const<_T>::value,
        std::vector<typename std::remove_const<_T>::type> const*, std::vector<_T>*>::type _data)
      : m_data(_data->data()), m_count(_data->size())
    {}

    /**
     * @brief Creates a reference to the data in a string (reference version)
     * @param _data Reference to the string containing the data
     */
    vector_ref(
        typename std::conditional<std::is_const<_T>::value, std::string const&, std::string&>::type
            _data)
      : m_data(reinterpret_cast<_T*>(_data.data())), m_count(_data.size() / sizeof(_T))
    {}

#if DEV_LDB
    /**
     * @brief Creates a reference from a LevelDB slice
     * @param _s LevelDB slice to reference
     */
    vector_ref(ldb::Slice const& _s)
      : m_data(reinterpret_cast<_T*>(_s.data())), m_count(_s.size() / sizeof(_T))
    {}
#endif

    /**
     * @brief Check if the reference points to valid data
     * @return true if both data pointer and count are non-zero
     */
    explicit operator bool() const { return m_data && m_count; }

    /**
     * @brief Compare the content with a vector
     * @param _c Vector to compare with
     * @return true if contents are equal
     */
    bool contentsEqual(std::vector<mutable_value_type> const& _c) const
    {
        if (!m_data || m_count == 0)
            return _c.empty();
        return _c.size() == m_count && !memcmp(_c.data(), m_data, m_count * sizeof(_T));
    }

    /**
     * @brief Convert the referenced data to a std::vector
     * @return A new vector containing copies of the referenced elements
     */
    std::vector<mutable_value_type> toVector() const
    {
        return std::vector<mutable_value_type>(m_data, m_data + m_count);
    }

    /**
     * @brief Convert the referenced data to a byte vector
     * @return A new vector containing the raw bytes
     */
    std::vector<unsigned char> toBytes() const
    {
        return std::vector<unsigned char>(reinterpret_cast<unsigned char const*>(m_data),
            reinterpret_cast<unsigned char const*>(m_data) + m_count * sizeof(_T));
    }

    /**
     * @brief Convert the referenced data to a string
     * @return A string containing the raw bytes
     */
    std::string toString() const
    {
        return std::string(reinterpret_cast<char const*>(m_data),
            reinterpret_cast<char const*>(m_data) + m_count * sizeof(_T));
    }

    /**
     * @brief Convert to a vector_ref of a different type
     * @tparam _T2 Target type
     * @return A new vector_ref with reinterpreted type
     * @note This operation requires compatible memory layouts
     */
    template <class _T2>
    explicit operator vector_ref<_T2>() const
    {
        assert(m_count * sizeof(_T) / sizeof(_T2) * sizeof(_T2) / sizeof(_T) == m_count);
        return vector_ref<_T2>(reinterpret_cast<_T2*>(m_data), m_count * sizeof(_T) / sizeof(_T2));
    }

    /**
     * @brief Convert to a const reference
     * @return A const reference to the same data
     */
    operator vector_ref<_T const>() const { return vector_ref<_T const>(m_data, m_count); }

    /**
     * @brief Get raw pointer to the data
     * @return Pointer to the first element
     */
    _T* data() const { return m_data; }

    /**
     * @brief Get the number of elements in the reference
     * @return Count of elements
     */
    size_t count() const { return m_count; }

    /**
     * @brief Get the number of elements (alias for count)
     * @return Count of elements
     */
    size_t size() const { return m_count; }

    /**
     * @brief Check if the reference is empty
     * @return true if no elements are referenced
     */
    bool empty() const { return !m_count; }

    /**
     * @brief Create a new reference to the next chunk of elements
     * @return A new reference starting after this one
     */
    vector_ref<_T> next() const
    {
        if (!m_data)
            return *this;
        return vector_ref<_T>(m_data + m_count, m_count);
    }

    /**
     * @brief Create a new reference to a subset of this data
     * @param _begin Start index
     * @param _count Number of elements to include (use ~size_t(0) for "until end")
     * @return A new vector_ref to the specified subset
     */
    vector_ref<_T> cropped(size_t _begin, size_t _count) const
    {
        if (m_data && _begin <= m_count && _count <= m_count && _begin + _count <= m_count)
            return vector_ref<_T>(
                m_data + _begin, _count == ~size_t(0) ? m_count - _begin : _count);
        return {};
    }

    /**
     * @brief Create a new reference starting at an offset
     * @param _begin Start index
     * @return A new vector_ref starting at the specified index
     */
    vector_ref<_T> cropped(size_t _begin) const
    {
        if (m_data && _begin <= m_count)
            return vector_ref<_T>(m_data + _begin, m_count - _begin);
        return {};
    }

    /**
     * @brief Change what this reference points to
     * @param _d New data pointer
     * @param _s New size
     */
    void retarget(_T* _d, size_t _s)
    {
        m_data = _d;
        m_count = _s;
    }

    /**
     * @brief Retarget to point to a vector
     * @param _t Vector to point to
     */
    void retarget(std::vector<_T> const& _t)
    {
        m_data = _t.data();
        m_count = _t.size();
    }

    /**
     * @brief Check if this reference overlaps with another
     * @tparam T Type of the other reference
     * @param _t Other reference to check
     * @return true if memory regions overlap
     */
    template <class T>
    bool overlapsWith(vector_ref<T> _t) const
    {
        void const* f1 = data();
        void const* t1 = data() + size();
        void const* f2 = _t.data();
        void const* t2 = _t.data() + _t.size();
        return f1 < t2 && t1 > f2;
    }

    /**
     * @brief Copy the content to another reference
     * @param _t Destination reference
     * @note Uses memmove if regions overlap, memcpy otherwise
     */
    void copyTo(vector_ref<typename std::remove_const<_T>::type> _t) const
    {
        if (overlapsWith(_t))
            memmove(_t.data(), m_data, std::min(_t.size(), m_count) * sizeof(_T));
        else
            memcpy(_t.data(), m_data, std::min(_t.size(), m_count) * sizeof(_T));
    }

    /**
     * @brief Copy the content to another reference and zero any extra elements
     * @param _t Destination reference
     */
    void populate(vector_ref<typename std::remove_const<_T>::type> _t) const
    {
        copyTo(_t);
        memset(_t.data() + m_count, 0, (_t.size() - m_count) * sizeof(_T));
    }

    /**
     * @brief Securely overwrite the memory to prevent data leakage
     * @note Adapted from OpenSSL's implementation
     */
    void cleanse()
    {
        static unsigned char s_cleanseCounter = 0;
        auto* p = reinterpret_cast<uint8_t*>(begin());
        size_t const len = reinterpret_cast<uint8_t*>(end()) - p;
        size_t loop = len;
        size_t count = s_cleanseCounter;

        while (loop--)
        {
            *(p++) = static_cast<uint8_t>(count);
            count += (17 + (reinterpret_cast<size_t>(p) & 0xf));
        }

        p = reinterpret_cast<uint8_t*>(
            memchr(reinterpret_cast<uint8_t*>(begin()), static_cast<uint8_t>(count), len));
        if (p)
            count += (63 + reinterpret_cast<size_t>(p));

        s_cleanseCounter = static_cast<uint8_t>(count);
        memset(reinterpret_cast<uint8_t*>(begin()), 0, len);
    }

    /**
     * @brief Get iterator to the beginning of the data
     * @return Pointer to the first element
     */
    _T* begin() { return m_data; }

    /**
     * @brief Get iterator to the end of the data
     * @return Pointer one past the last element
     */
    _T* end() { return m_data + m_count; }

    /**
     * @brief Get const iterator to the beginning of the data
     * @return Const pointer to the first element
     */
    _T const* begin() const { return m_data; }

    /**
     * @brief Get const iterator to the end of the data
     * @return Const pointer one past the last element
     */
    _T const* end() const { return m_data + m_count; }

    /**
     * @brief Access element at index
     * @param _i Index of the element
     * @return Reference to the element
     */
    _T& operator[](size_t _i)
    {
        assert(m_data);
        assert(_i < m_count);
        return m_data[_i];
    }

    /**
     * @brief Access element at index (const version)
     * @param _i Index of the element
     * @return Const reference to the element
     */
    _T const& operator[](size_t _i) const
    {
        assert(m_data);
        assert(_i < m_count);
        return m_data[_i];
    }

    /**
     * @brief Equality comparison
     * @param _cmp Vector reference to compare with
     * @return true if both references point to the same memory region
     */
    bool operator==(vector_ref<_T> const& _cmp) const
    {
        return m_data == _cmp.m_data && m_count == _cmp.m_count;
    }

    /**
     * @brief Inequality comparison
     * @param _cmp Vector reference to compare with
     * @return true if references point to different memory regions
     */
    bool operator!=(vector_ref<_T> const& _cmp) const { return !operator==(_cmp); }

    /**
     * @brief Reset to an empty reference
     */
    void reset()
    {
        m_data = nullptr;
        m_count = 0;
    }

private:
    _T* m_data;      ///< Pointer to the referenced data
    size_t m_count;  ///< Number of elements being referenced
};

/**
 * @brief Create a reference to a single constant object
 * @tparam _T Type of the object
 * @param _t Object to reference
 * @return A vector_ref to the object
 */
template <class _T>
vector_ref<_T const> ref(_T const& _t)
{
    return vector_ref<_T const>(&_t, 1);
}

/**
 * @brief Create a reference to a single mutable object
 * @tparam _T Type of the object
 * @param _t Object to reference
 * @return A vector_ref to the object
 */
template <class _T>
vector_ref<_T> ref(_T& _t)
{
    return vector_ref<_T>(&_t, 1);
}

/**
 * @brief Create a reference to a constant vector
 * @tparam _T Type of the vector elements
 * @param _t Vector to reference
 * @return A vector_ref to the vector's data
 */
template <class _T>
vector_ref<_T const> ref(std::vector<_T> const& _t)
{
    return vector_ref<_T const>(&_t);
}

/**
 * @brief Create a reference to a mutable vector
 * @tparam _T Type of the vector elements
 * @param _t Vector to reference
 * @return A vector_ref to the vector's data
 */
template <class _T>
vector_ref<_T> ref(std::vector<_T>& _t)
{
    return vector_ref<_T>(&_t);
}

}  // namespace dev
