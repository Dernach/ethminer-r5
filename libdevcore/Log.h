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
/** @file Log.h
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 *
 * The logging subsystem.
 */

#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

#include "Common.h"
#include "CommonData.h"
#include "FixedHash.h"
#include "Terminal.h"
#include "vector_ref.h"

/// Logging system verbosity options
#define LOG_JSON 1           ///< Log JSON messages
#define LOG_PER_GPU 2        ///< Log per-GPU statistics
#define LOG_CONNECT 32       ///< Log connection events
#define LOG_SWITCH 64        ///< Log mining switch events
#define LOG_SUBMIT 128       ///< Log solution submission events
#define LOG_PROGRAMFLOW 256  ///< Log general program flow
#define LOG_NEXT 512         ///< Reserved for future use

/**
 * @brief Debug logging macro for program flow, enabled only in debug builds
 */
#if DEV_BUILD
#define DEV_BUILD_LOG_PROGRAMFLOW(_S, _V) \
    if (g_logOptions & LOG_PROGRAMFLOW)   \
    {                                     \
        _S << _V;                         \
    }                                     \
    ((void)(0))
#else
#define DEV_BUILD_LOG_PROGRAMFLOW(_S, _V) ((void)(0))
#endif

/// Global logging options
extern int g_logOptions;
/// Disable color output
extern bool g_logNoColor;
/// Log to syslog
extern bool g_logSyslog;
/// Log to stdout
extern bool g_logStdout;

namespace dev
{

/**
 * @brief Output a debug message to the log
 * @param _s Message to log
 */
void simpleDebugOut(std::string const& _s);

/**
 * @brief Set the name of the current thread for logging
 * @param _n Thread name
 */
void setThreadName(char const* _n);

/**
 * @brief Get the name of the current thread
 * @return std::string Thread name
 */
std::string getThreadName();

/**
 * @brief Base class for all log channels
 */
struct LogChannel
{
    /**
     * @brief Get the channel's name/prefix
     * @return const char* Channel identifier
     */
    static const char* name();
};

/**
 * @brief Warning log channel
 */
struct WarnChannel : public LogChannel
{
    /**
     * @brief Get the channel's name/prefix
     * @return const char* Channel identifier
     */
    static const char* name();
};

/**
 * @brief Informational note log channel
 */
struct NoteChannel : public LogChannel
{
    /**
     * @brief Get the channel's name/prefix
     * @return const char* Channel identifier
     */
    static const char* name();
};

/**
 * @brief Base class for log output streams
 *
 * Handles the formatting of log entries with timestamps and thread info
 */
class LogOutputStreamBase
{
public:
    /**
     * @brief Construct a new log output stream
     * @param _id Channel identifier
     */
    explicit LogOutputStreamBase(char const* _id);

    /**
     * @brief Append data to the log stream
     * @tparam T Type of data to append
     * @param _t Data to append
     */
    template <class T>
    void append(T const& _t)
    {
        m_sstr << _t;
    }

protected:
    std::stringstream m_sstr;  ///< The accrued log entry
};

/**
 * @brief Logging class with iostream-like interface
 *
 * Provides a convenient way to format and output log messages
 *
 * @tparam Id The log channel identifier type
 */
template <class Id>
class LogOutputStream : public LogOutputStreamBase
{
public:
    /**
     * @brief Construct a new log output stream object
     */
    LogOutputStream() : LogOutputStreamBase(Id::name()) {}

    /**
     * @brief Destructor - outputs the log message
     */
    ~LogOutputStream() { simpleDebugOut(m_sstr.str()); }

    /**
     * @brief Stream insertion operator
     * @tparam T Type of data to append
     * @param _t Data to append to the log
     * @return LogOutputStream& Reference to this stream for chaining
     */
    template <class T>
    LogOutputStream& operator<<(T const& _t)
    {
        append(_t);
        return *this;
    }
};

/**
 * @brief Helper macro to create a log stream for the specified channel
 */
#define clog(X) dev::LogOutputStream<X>()

/**
 * @brief Simplified stream objects for common log channels
 */
#define cnote clog(dev::NoteChannel)
#define cwarn clog(dev::WarnChannel)

}  // namespace dev
