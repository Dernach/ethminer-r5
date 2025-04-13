/*
 This file is part of ethereum.

 ethminer is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ethereum is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ethminer.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "Miner.h"
#include <chrono>
#include <string>

namespace dev
{
namespace eth
{

// Static member initialization
unsigned Miner::s_dagLoadMode = 0;
unsigned Miner::s_dagLoadIndex = 0;
unsigned Miner::s_minersCount = 0;

FarmFace* FarmFace::m_this = nullptr;

DeviceDescriptor Miner::getDescriptor() const
{
    return m_deviceDescriptor;
}

void Miner::setWork(const WorkPackage& _work)
{
    {
        std::lock_guard<std::mutex> lock(m_workMutex);

        // Void work if this miner is paused
        if (paused())
        {
            m_work.header = h256();
        }
        else
        {
            m_work = _work;
        }

#ifdef DEV_BUILD
        m_workSwitchStart = std::chrono::steady_clock::now();
#endif
    }

    kick_miner();
}

void Miner::pause(MinerPauseEnum what)
{
    std::lock_guard<std::mutex> lock(m_pauseMutex);
    m_pauseFlags.set(what);
    m_work.header = h256();
    kick_miner();
}

bool Miner::paused() const
{
    std::lock_guard<std::mutex> lock(m_pauseMutex);
    return m_pauseFlags.any();
}

bool Miner::pauseTest(MinerPauseEnum what) const
{
    std::lock_guard<std::mutex> lock(m_pauseMutex);
    return m_pauseFlags.test(what);
}

std::string Miner::pausedString() const
{
    std::lock_guard<std::mutex> lock(m_pauseMutex);

    if (!m_pauseFlags.any())
    {
        return "";
    }

    std::string result;

    static const std::pair<MinerPauseEnum, const char*> pauseReasons[] = {
        {PauseDueToOverHeating, "Overheating"}, {PauseDueToAPIRequest, "Api request"},
        {PauseDueToFarmPaused, "Farm suspended"},
        {PauseDueToInsufficientMemory, "Insufficient GPU memory"},
        {PauseDueToInitEpochError, "Epoch initialization error"}};

    for (const auto& reason : pauseReasons)
    {
        if (m_pauseFlags.test(reason.first))
        {
            if (!result.empty())
            {
                result.append("; ");
            }
            result.append(reason.second);
        }
    }

    return result;
}

void Miner::resume(MinerPauseEnum fromWhat)
{
    std::lock_guard<std::mutex> lock(m_pauseMutex);
    m_pauseFlags.reset(fromWhat);
    // Note: Original commented code preserved intentionally
    // if (!m_pauseFlags.any())
    //{
    //    // TODO Push most recent job from farm ?
    //    // If we do not push a new job the miner will stay idle
    //    // till a new job arrives
    //}
}

float Miner::RetrieveHashRate() const noexcept
{
    return m_hashRate.load(std::memory_order_relaxed);
}

void Miner::TriggerHashRateUpdate() noexcept
{
    bool expected = false;
    if (m_hashRateUpdate.compare_exchange_strong(expected, true, std::memory_order_relaxed))
    {
        return;
    }
    // GPU didn't respond to last trigger, assume it's dead.
    // This can happen on CUDA if:
    //   runtime of --cuda-grid-size * --cuda-streams exceeds time of m_collectInterval
    m_hashRate.store(0.0f, std::memory_order_relaxed);
}

bool Miner::initEpoch()
{
    // When loading of DAG is sequential wait for
    // this instance to become current
    if (s_dagLoadMode == DAG_LOAD_MODE_SEQUENTIAL)
    {
        using namespace std::chrono;
        using namespace std::chrono_literals;

        auto timeout = 3s;

        while (s_dagLoadIndex < m_index)
        {
            if (shouldStop())
            {
                return false;
            }

            std::unique_lock<std::mutex> lock(m_workMutex);
            if (m_dag_loaded_signal.wait_for(lock, timeout) == std::cv_status::timeout)
            {
                // Continue waiting
            }
        }

        if (shouldStop())
        {
            return false;
        }
    }

    // Run the internal initialization specific for miner
    bool result = initEpoch_internal();

    // Advance to next miner or reset to zero for
    // next run if all have processed
    if (s_dagLoadMode == DAG_LOAD_MODE_SEQUENTIAL)
    {
        s_dagLoadIndex = (m_index + 1);
        if (s_minersCount == s_dagLoadIndex)
        {
            s_dagLoadIndex = 0;
        }
        else
        {
            m_dag_loaded_signal.notify_all();
        }
    }

    return result;
}

WorkPackage Miner::work() const
{
    std::lock_guard<std::mutex> lock(m_workMutex);
    return m_work;
}

void Miner::updateHashRate(uint32_t _groupSize, uint32_t _increment) noexcept
{
    m_groupCount += _increment;

    bool expected = true;
    if (!m_hashRateUpdate.compare_exchange_strong(expected, false, std::memory_order_relaxed))
    {
        return;
    }

    using namespace std::chrono;
    auto now = steady_clock::now();

    int64_t microsecondsDuration = duration_cast<microseconds>(now - m_hashTime).count();

    m_hashTime = now;

    float newHashRate = 0.0f;
    if (microsecondsDuration > 0)
    {
        newHashRate = static_cast<float>(m_groupCount * _groupSize) * 1.0e6f / microsecondsDuration;
    }

    m_hashRate.store(newHashRate, std::memory_order_relaxed);
    m_groupCount = 0;
}

}  // namespace eth
}  // namespace dev
