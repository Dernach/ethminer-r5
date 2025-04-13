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
/** @file Exceptions.h
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#pragma once

#include <exception>
#include <string>

#include <boost/exception/all.hpp>
#include <boost/throw_exception.hpp>

#include "CommonData.h"
#include "FixedHash.h"

namespace dev
{
/**
 * @brief Base class for all exceptions in the library.
 *
 * Provides common functionality for all exception types, including
 * custom message support and integration with boost::exception.
 */
struct Exception : virtual std::exception, virtual boost::exception
{
    /**
     * @brief Construct a new Exception object
     * @param _message Optional custom error message
     */
    explicit Exception(std::string _message = std::string()) : m_message(std::move(_message)) {}

    /**
     * @brief Returns the error message
     * @return const char* Pointer to error message or std::exception::what()
     */
    const char* what() const noexcept override
    {
        return m_message.empty() ? std::exception::what() : m_message.c_str();
    }

private:
    std::string m_message;  ///< Custom error message
};

/**
 * @brief Macro to create simple exception types
 *
 * Creates a new exception type that inherits from Exception
 * and overrides what() to return the type name
 */
#define DEV_SIMPLE_EXCEPTION(X)                    \
    struct X : virtual Exception                   \
    {                                              \
        const char* what() const noexcept override \
        {                                          \
            return #X;                             \
        }                                          \
    }

// Common exception types
DEV_SIMPLE_EXCEPTION(BadHexCharacter);

/**
 * @brief Exception thrown when an external function fails
 */
struct ExternalFunctionFailure : virtual Exception
{
public:
    /**
     * @brief Construct a new ExternalFunctionFailure object
     * @param _f Name of the failed function
     */
    explicit ExternalFunctionFailure(const std::string& _f)
      : Exception("Function " + _f + "() failed.")
    {}
};

// Error information types to be added to exceptions
using errinfo_invalidSymbol = boost::error_info<struct tag_invalidSymbol, char>;
using errinfo_comment = boost::error_info<struct tag_comment, std::string>;
using errinfo_required = boost::error_info<struct tag_required, bigint>;
using errinfo_got = boost::error_info<struct tag_got, bigint>;
using RequirementError = boost::tuple<errinfo_required, errinfo_got>;

}  // namespace dev
