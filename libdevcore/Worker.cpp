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
/** @file Worker.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#include "Worker.h"

#include <chrono>
#include <thread>

#include "Log.h"

using namespace std;
using namespace dev;

void Worker::startWorking()
{
    DEV_BUILD_LOG_PROGRAMFLOW(cnote, "Worker::startWorking() begin");

    Guard l(x_work);
    if (m_work)
    {
        // If we have a thread already, try to restart it
        WorkerState expectedState = WorkerState::Stopped;
        m_state.compare_exchange_strong(expectedState, WorkerState::Starting);
    }
    else
    {
        // No thread exists yet, create a new one
        m_state = WorkerState::Starting;
        m_work.reset(new thread([this]() {
            // Set thread name for debugging
            setThreadName(m_name.c_str());

            // Main thread loop
            while (m_state != WorkerState::Killing)
            {
                // Transition from Starting to Started state
                WorkerState expectedState = WorkerState::Starting;
                bool transitionSucceeded =
                    m_state.compare_exchange_strong(expectedState, WorkerState::Started);
                (void)transitionSucceeded;  // Avoid unused variable warning

                try
                {
                    // Call the derived class implementation
                    workLoop();
                }
                catch (std::exception const& _e)
                {
                    // Handle exceptions in worker thread
                    clog(WarnChannel) << "Exception thrown in Worker thread: " << _e.what();
                    if (g_exitOnError)
                    {
                        clog(WarnChannel) << "Terminating due to --exit";
                        raise(SIGTERM);
                    }
                }

                // Transition to Stopped state
                WorkerState previousState = m_state.exchange(WorkerState::Stopped);

                // Preserve Killing or Starting states
                if (previousState == WorkerState::Killing || previousState == WorkerState::Starting)
                    m_state.exchange(previousState);

                // Wait while in Stopped state
                while (m_state == WorkerState::Stopped)
                    this_thread::sleep_for(chrono::milliseconds(20));
            }
        }));
    }

    // Wait until the thread is fully started
    while (m_state == WorkerState::Starting)
        this_thread::sleep_for(chrono::microseconds(20));

    DEV_BUILD_LOG_PROGRAMFLOW(cnote, "Worker::startWorking() end");
}

void Worker::triggerStopWorking()
{
    DEV_GUARDED(x_work)
    if (m_work)
    {
        // Only transition from Started to Stopping
        WorkerState expectedState = WorkerState::Started;
        m_state.compare_exchange_strong(expectedState, WorkerState::Stopping);
    }
}

void Worker::stopWorking()
{
    DEV_BUILD_LOG_PROGRAMFLOW(cnote, "Worker::stopWorking() begin");

    DEV_GUARDED(x_work)
    if (m_work)
    {
        // Signal the thread to stop
        WorkerState expectedState = WorkerState::Started;
        m_state.compare_exchange_strong(expectedState, WorkerState::Stopping);

        // Wait for the thread to actually stop
        DEV_BUILD_LOG_PROGRAMFLOW(
            cnote, "Worker::stopWorking() waiting for WorkerState::Stopped begin");
        while (m_state != WorkerState::Stopped)
            this_thread::sleep_for(chrono::microseconds(20));
        DEV_BUILD_LOG_PROGRAMFLOW(
            cnote, "Worker::stopWorking() waiting for WorkerState::Stopped end");
    }

    DEV_BUILD_LOG_PROGRAMFLOW(cnote, "Worker::stopWorking() end");
}

Worker::~Worker()
{
    DEV_BUILD_LOG_PROGRAMFLOW(cnote, "Worker::~Worker() begin");

    DEV_GUARDED(x_work)
    if (m_work)
    {
        // Signal thread termination
        m_state.exchange(WorkerState::Killing);

        // Wait for thread to exit and clean up
        m_work->join();
        m_work.reset();
    }

    DEV_BUILD_LOG_PROGRAMFLOW(cnote, "Worker::~Worker() end");
}
