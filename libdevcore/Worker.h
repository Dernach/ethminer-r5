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
/** @file Worker.h
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#pragma once

#include <signal.h>
#include <atomic>
#include <cassert>
#include <memory>
#include <string>
#include <thread>

#include "Guards.h"

/**
 * @brief Global flag to control error handling behavior
 * If true, the application will exit on encountering errors
 */
extern bool g_exitOnError;

namespace dev
{

/**
 * @brief Enumeration of possible worker thread states
 */
enum class WorkerState
{
    Starting,  ///< Worker is starting up
    Started,   ///< Worker is active and running
    Stopping,  ///< Worker is in the process of stopping
    Stopped,   ///< Worker has stopped and is idle
    Killing    ///< Worker is being terminated
};

/**
 * @brief Base class for worker threads in the application
 *
 * Provides a standard way to manage background worker threads
 * with proper lifecycle control and state management.
 */
class Worker
{
public:
    /**
     * @brief Create a new worker with the given name
     * @param _name Name for the worker thread (used for logging)
     */
    explicit Worker(std::string _name) : m_name(std::move(_name)) {}

    // Prevent copying
    Worker(Worker const&) = delete;
    Worker& operator=(Worker const&) = delete;

    /**
     * @brief Virtual destructor ensures proper cleanup of derived classes
     */
    virtual ~Worker();

    /**
     * @brief Start the worker thread
     *
     * Creates and starts a new thread to execute workLoop().
     * Returns once the thread has fully started.
     */
    void startWorking();

    /**
     * @brief Signal the worker thread to stop
     *
     * Doesn't block waiting for the thread to actually stop.
     */
    void triggerStopWorking();

    /**
     * @brief Stop the worker thread and wait for it to finish
     *
     * Changes the thread state to stopping and blocks until
     * the thread has completely stopped.
     */
    void stopWorking();

    /**
     * @brief Check if the worker should stop working
     * @return true if the worker is not in the Started state
     */
    bool shouldStop() const { return m_state != WorkerState::Started; }

protected:
    /**
     * @brief Main work method to be implemented by derived classes
     *
     * This method is called on the worker thread and should contain
     * the main processing loop.
     */
    virtual void workLoop() = 0;

private:
    std::string m_name;                   ///< Worker name for logging
    mutable Mutex x_work;                 ///< Mutex protecting the thread management
    std::unique_ptr<std::thread> m_work;  ///< The worker thread
    std::atomic<WorkerState> m_state{WorkerState::Starting};  ///< Current state of the worker
};

}  // namespace dev
