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

#include "Log.h"

#include <iostream>
#include <map>
#include <thread>

#ifdef __APPLE__
#include <pthread.h>
#endif

#include "Guards.h"

using namespace std;
using namespace dev;

// Unicode symbols for log messages
// ⊳⊲◀▶■▣▢□▷◁▧▨▩▲◆◉◈◇◎●◍◌○◼☑☒☎☢☣☰☀♽♥♠✩✭❓✔✓✖✕✘✓✔✅⚒⚡⦸⬌∅⁕«««»»»⚙

// Global logging configuration
int g_logOptions = 0;
bool g_logNoColor = false;
bool g_logSyslog = false;
bool g_logStdout = false;

const char* LogChannel::name()
{
    return EthGray "..";
}

const char* WarnChannel::name()
{
    return EthRed " X";
}

const char* NoteChannel::name()
{
    return EthBlue " i";
}

LogOutputStreamBase::LogOutputStreamBase(char const* _id)
{
    // Set locale for proper formatting
    static std::locale logLocl = std::locale("");
    m_sstr.imbue(logLocl);

    if (g_logSyslog)
    {
        // Simplified format for syslog
        m_sstr << std::left << std::setw(8) << getThreadName() << " " EthReset;
    }
    else
    {
        // Full format with timestamp for normal output
        time_t rawTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        char buf[24];
        if (strftime(buf, 24, "%X", localtime(&rawTime)) == 0)
            buf[0] = '\0';  // Empty if strftime fails

        m_sstr << _id << " " EthViolet << buf << " " EthBlue << std::left << std::setw(8)
               << getThreadName() << " " EthReset;
    }
}

/**
 * @brief Thread-local storage for thread names
 */
struct ThreadLocalLogName
{
    /**
     * @brief Construct a new Thread Local Log Name object
     * @param _name Thread name
     */
    explicit ThreadLocalLogName(char const* _name) { name = _name; }

    /**
     * @brief Thread-local name storage
     */
    thread_local static char const* name;
};

// Initialize thread-local storage
thread_local char const* ThreadLocalLogName::name;

// Default thread name
ThreadLocalLogName g_logThreadName("main");

string dev::getThreadName()
{
#if defined(__linux__) || defined(__APPLE__)
    // Use platform-specific thread naming on Unix-like systems
    char buffer[128];
    pthread_getname_np(pthread_self(), buffer, 127);
    buffer[127] = 0;
    return buffer;
#else
    // Fall back to our custom thread naming on other platforms
    return ThreadLocalLogName::name ? ThreadLocalLogName::name : "<unknown>";
#endif
}

void dev::setThreadName(char const* _n)
{
#if defined(__linux__)
    // Linux-specific thread naming
    pthread_setname_np(pthread_self(), _n);
#elif defined(__APPLE__)
    // macOS-specific thread naming
    pthread_setname_np(_n);
#else
    // Custom thread naming for other platforms
    ThreadLocalLogName::name = _n;
#endif
}

void dev::simpleDebugOut(std::string const& _s)
{
    try
    {
        // Determine output stream based on configuration
        std::ostream& os = g_logStdout ? std::cout : std::clog;

        if (!g_logNoColor)
        {
            // If colors are enabled, output directly
            os << _s << '\n';
            os.flush();
            return;
        }

        // If colors are disabled, strip ANSI color codes
        bool skip = false;
        std::stringstream ss;

        for (char c : _s)
        {
            if (!skip && c == '\x1b')
                skip = true;
            else if (skip && c == 'm')
                skip = false;
            else if (!skip)
                ss << c;
        }

        ss << '\n';
        os << ss.str();
        os.flush();
    }
    catch (...)
    {
        // Swallow any exceptions that might occur during logging
        return;
    }
}
