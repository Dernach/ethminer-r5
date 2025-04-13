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
/** @file Guards.h
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#pragma once

#include <atomic>
#include <mutex>

namespace dev
{

/**
 * @brief Type aliases for standard mutex and guards for simplified syntax
 */
using Mutex = std::mutex;
using Guard = std::lock_guard<std::mutex>;
using UniqueGuard = std::unique_lock<std::mutex>;

/**
 * @brief Generic guard wrapper that adds a boolean flag for use in for-loops
 *
 * This is primarily used for implementing scoped mutex macros
 *
 * @tparam GuardType The type of guard to wrap (e.g. std::lock_guard)
 * @tparam MutexType The type of mutex to use (e.g. std::mutex)
 */
template <class GuardType, class MutexType>
struct GenericGuardBool : GuardType
{
    /**
     * @brief Construct a new Generic Guard Bool object
     * @param _m The mutex to lock
     */
    explicit GenericGuardBool(MutexType& _m) : GuardType(_m) {}

    /**
     * @brief Boolean flag used to control the containing for-loop
     */
    bool b = true;
};

/**
 * @brief Simple scoped mutex lock macro for guarding code blocks
 *
 * This macro creates a scoped mutex lock that guards the following statement
 * or block. The mutex is locked at the beginning of the scope and automatically
 * unlocked when execution leaves the scope.
 *
 * Usage:
 * @code
 * Mutex m;
 * unsigned d;
 *
 * // Guard a single statement
 * DEV_GUARDED(m) d = 1;
 *
 * // Guard a block
 * DEV_GUARDED(m) {
 *   for (auto d = 10; d > 0; --d) foo(d);
 *   d = 0;
 * }
 * @endcode
 *
 * @param MUTEX The mutex to lock
 */
#define DEV_GUARDED(MUTEX)                                                              \
    for (::dev::GenericGuardBool<::dev::Guard, ::dev::Mutex> __eth_l(MUTEX); __eth_l.b; \
        __eth_l.b = false)

}  // namespace dev
