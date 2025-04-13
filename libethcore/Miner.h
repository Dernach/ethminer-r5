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

#pragma once

#include <atomic>
#include <bitset>
#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <list>
#include <memory>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include "EthashAux.h"
#include <libdevcore/Common.h>
#include <libdevcore/Log.h>
#include <libdevcore/Worker.h>

#include <boost/format.hpp>

// DAG load modes
constexpr int DAG_LOAD_MODE_PARALLEL = 0;
constexpr int DAG_LOAD_MODE_SEQUENTIAL = 1;

namespace dev
{
namespace eth
{

// Enumerations
enum class DeviceTypeEnum
{
    Unknown,
    Cpu,
    Gpu,
    Accelerator
};

enum class DeviceSubscriptionTypeEnum
{
    None,
    OpenCL,
    Cuda,
    Cpu
};

enum class MinerType
{
    Mixed,
    CL,
    CUDA,
    CPU
};

enum class HwMonitorInfoType
{
    UNKNOWN,
    NVIDIA,
    AMD,
    CPU
};

enum class ClPlatformTypeEnum
{
    Unknown,
    Amd,
    Clover,
    Nvidia,
    Intel
};

enum class SolutionAccountingEnum
{
    Accepted,
    Rejected,
    Wasted,
    Failed
};

// Forward declarations
class Miner;
class FarmFace;

// Structures
struct MinerSettings
{
    std::vector<unsigned> devices;
};

// CUDA Miner settings
struct CUSettings : public MinerSettings
{
    unsigned streams = 2;
    unsigned schedule = 4;
    unsigned gridSize = 288;
    unsigned blockSize = 256;
};

// OpenCL Miner settings
struct CLSettings : public MinerSettings
{
    bool noBinary = false;
    bool noExit = false;
    unsigned globalWorkSize = 0;
    unsigned globalWorkSizeMultiplier = 65536;
    unsigned localWorkSize = 128;
};

// CPU Miner settings
struct CPSettings : public MinerSettings
{
};

struct SolutionAccountType
{
    unsigned accepted = 0;
    unsigned rejected = 0;
    unsigned wasted = 0;
    unsigned failed = 0;
    std::chrono::steady_clock::time_point tstamp = std::chrono::steady_clock::now();

    std::string str() const
    {
        std::string result = "A" + std::to_string(accepted);

        if (wasted > 0)
        {
            result.append(":W" + std::to_string(wasted));
        }

        if (rejected > 0)
        {
            result.append(":R" + std::to_string(rejected));
        }

        if (failed > 0)
        {
            result.append(":F" + std::to_string(failed));
        }

        return result;
    }
};

struct HwSensorsType
{
    int tempC = 0;
    int fanP = 0;
    double powerW = 0.0;

    std::string str() const
    {
        std::string result = std::to_string(tempC) + "C " + std::to_string(fanP) + "%";

        if (powerW > 0.0)
        {
            result.append(" " + boost::str(boost::format("%.2f") % powerW) + "W");
        }

        return result;
    }
};

struct TelemetryAccountType
{
    std::string prefix = "";
    float hashrate = 0.0f;
    bool paused = false;
    HwSensorsType sensors;
    SolutionAccountType solutions;
};

struct DeviceDescriptor
{
    DeviceTypeEnum type = DeviceTypeEnum::Unknown;
    DeviceSubscriptionTypeEnum subscriptionType = DeviceSubscriptionTypeEnum::None;

    std::string uniqueId;    // For GPUs this is the PCI ID
    size_t totalMemory = 0;  // Total memory available by device
    std::string name;        // Device Name

    // OpenCL specific properties
    bool clDetected = false;
    std::string clName;
    unsigned int clPlatformId = 0;
    std::string clPlatformName;
    ClPlatformTypeEnum clPlatformType = ClPlatformTypeEnum::Unknown;
    std::string clPlatformVersion;
    unsigned int clPlatformVersionMajor = 0;
    unsigned int clPlatformVersionMinor = 0;
    unsigned int clDeviceOrdinal = 0;
    unsigned int clDeviceIndex = 0;
    std::string clDeviceVersion;
    unsigned int clDeviceVersionMajor = 0;
    unsigned int clDeviceVersionMinor = 0;
    std::string clBoardName;
    size_t clMaxMemAlloc = 0;
    size_t clMaxWorkGroup = 0;
    unsigned int clMaxComputeUnits = 0;
    std::string clNvCompute;
    unsigned int clNvComputeMajor = 0;
    unsigned int clNvComputeMinor = 0;

    // CUDA specific properties
    bool cuDetected = false;
    std::string cuName;
    unsigned int cuDeviceOrdinal = 0;
    unsigned int cuDeviceIndex = 0;
    std::string cuCompute;
    unsigned int cuComputeMajor = 0;
    unsigned int cuComputeMinor = 0;

    // CPU specific properties
    int cpCpuNumer = 0;
};

struct HwMonitorInfo
{
    HwMonitorInfoType deviceType = HwMonitorInfoType::UNKNOWN;
    std::string devicePciId;
    int deviceIndex = -1;
};

/// Pause mining reasons
enum MinerPauseEnum
{
    PauseDueToOverHeating,
    PauseDueToAPIRequest,
    PauseDueToFarmPaused,
    PauseDueToInsufficientMemory,
    PauseDueToInitEpochError,
    Pause_MAX  // Must always be last as a placeholder of max count
};

/// Keeps track of progress for farm and miners
struct TelemetryType
{
    bool hwmon = false;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    TelemetryAccountType farm;
    std::vector<TelemetryAccountType> miners;

    std::string str() const
    {
        std::stringstream result;

        /*
        Calculate duration
        */
        auto duration = std::chrono::steady_clock::now() - start;
        auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
        int hoursSize = (hours.count() > 9 ? (hours.count() > 99 ? 3 : 2) : 1);
        duration -= hours;
        auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration);

        result << EthGreen << std::setw(hoursSize) << hours.count() << ":" << std::setfill('0')
               << std::setw(2) << minutes.count() << EthReset << EthWhiteBold << " "
               << farm.solutions.str() << EthReset << " ";

        /*
        Scale hashrates appropriately
        */
        static const std::string suffixes[] = {"h", "Kh", "Mh", "Gh"};
        float hr = farm.hashrate;
        int magnitude = 0;

        while (hr > 1000.0f && magnitude <= 3)
        {
            hr /= 1000.0f;
            magnitude++;
        }

        result << EthTealBold << std::fixed << std::setprecision(2) << hr << " "
               << suffixes[magnitude] << EthReset << " - ";

        int i = -1;                                   // Current miner index
        int m = static_cast<int>(miners.size()) - 1;  // Max miner index

        for (const TelemetryAccountType& miner : miners)
        {
            i++;
            hr = miner.hashrate;
            if (hr > 0.0f)
            {
                hr /= std::pow(1000.0f, magnitude);
            }

            result << (miner.paused ? EthRed : "") << miner.prefix << i << " " << EthTeal
                   << std::fixed << std::setprecision(2) << hr << EthReset;

            if (hwmon)
            {
                result << " " << EthTeal << miner.sensors.str() << EthReset;
            }

            // Eventually push also solutions per single GPU
            if (g_logOptions & LOG_PER_GPU)
            {
                result << " " << EthTeal << miner.solutions.str() << EthReset;
            }

            // Separator if not the last miner index
            if (i < m)
            {
                result << ", ";
            }
        }

        return result.str();
    }
};

/**
 * @brief Class for hosting one or more Miners.
 * @warning Must be implemented in a threadsafe manner since it will be called from multiple
 * miner threads.
 */
class FarmFace
{
public:
    FarmFace() { m_this = this; }
    static FarmFace& f() { return *m_this; }

    virtual ~FarmFace() = default;
    virtual unsigned get_tstart() = 0;
    virtual unsigned get_tstop() = 0;
    virtual unsigned get_ergodicity() = 0;

    /**
     * @brief Called from a Miner to note a WorkPackage has a solution.
     * @param _p The solution.
     * @return true iff the solution was good (implying that mining should be).
     */
    virtual void submitProof(Solution const& _p) = 0;
    virtual void accountSolution(unsigned _minerIdx, SolutionAccountingEnum _accounting) = 0;
    virtual uint64_t get_nonce_scrambler() = 0;
    virtual unsigned get_segment_width() = 0;

private:
    static FarmFace* m_this;
};

/**
 * @brief A miner - a member and adoptee of the Farm.
 * @warning Not threadsafe. It is assumed Farm will synchronize calls to/from this class.
 */
class Miner : public Worker
{
public:
    Miner(const std::string& _name, unsigned _index)
      : Worker(_name + std::to_string(_index)), m_index(_index)
    {}

    virtual ~Miner() = default;

    // Sets basic info for eventual serialization of DAG load
    static void setDagLoadInfo(unsigned _mode, unsigned _devicecount)
    {
        s_dagLoadMode = _mode;
        s_dagLoadIndex = 0;
        s_minersCount = _devicecount;
    }

    /**
     * @brief Gets the device descriptor assigned to this instance
     */
    DeviceDescriptor getDescriptor() const;

    /**
     * @brief Assigns hashing work to this instance
     */
    void setWork(const WorkPackage& _work);

    /**
     * @brief Assigns Epoch context to this instance
     */
    void setEpoch(const EpochContext& _ec) { m_epochContext = _ec; }

    unsigned Index() const { return m_index; }

    HwMonitorInfo hwmonInfo() const { return m_hwmoninfo; }

    void setHwmonDeviceIndex(int i) { m_hwmoninfo.deviceIndex = i; }

    /**
     * @brief Kick an asleep miner.
     */
    virtual void kick_miner() = 0;

    /**
     * @brief Pauses mining setting a reason flag
     */
    void pause(MinerPauseEnum what);

    /**
     * @brief Whether or not this miner is paused for any reason
     */
    bool paused() const;

    /**
     * @brief Checks if the given reason for pausing is currently active
     */
    bool pauseTest(MinerPauseEnum what) const;

    /**
     * @brief Returns the human readable reason for this miner being paused
     */
    std::string pausedString() const;

    /**
     * @brief Cancels a pause flag.
     * @note Miner can be paused for multiple reasons at a time.
     */
    void resume(MinerPauseEnum fromwhat);

    /**
     * @brief Retrieves currently collected hashrate
     */
    float RetrieveHashRate() const noexcept;

    /**
     * @brief Triggers a hashrate update, or marks the miner as dead if it doesn't respond
     */
    void TriggerHashRateUpdate() noexcept;

protected:
    /**
     * @brief Initializes miner's device.
     */
    virtual bool initDevice() = 0;

    /**
     * @brief Initializes miner to current (or changed) epoch.
     */
    bool initEpoch();

    /**
     * @brief Miner's specific initialization to current (or changed) epoch.
     */
    virtual bool initEpoch_internal() = 0;

    /**
     * @brief Returns current workpackage this miner is working on
     */
    WorkPackage work() const;

    /**
     * @brief Updates the hash rate calculation based on latest work
     */
    void updateHashRate(uint32_t _groupSize, uint32_t _increment) noexcept;

    // Static configuration
    static unsigned s_minersCount;   // Total Number of Miners
    static unsigned s_dagLoadMode;   // Way dag should be loaded
    static unsigned s_dagLoadIndex;  // In case of serialized load of dag this is the index of miner
                                     // which should load next

    const unsigned m_index = 0;           // Ordinal index of the Instance (not the device)
    DeviceDescriptor m_deviceDescriptor;  // Info about the device

    EpochContext m_epochContext;

#ifdef DEV_BUILD
    std::chrono::steady_clock::time_point m_workSwitchStart;
#endif

    HwMonitorInfo m_hwmoninfo;
    mutable std::mutex m_workMutex;
    mutable std::mutex m_pauseMutex;
    std::condition_variable m_new_work_signal;
    std::condition_variable m_dag_loaded_signal;

private:
    std::bitset<MinerPauseEnum::Pause_MAX> m_pauseFlags;

    WorkPackage m_work;

    std::chrono::steady_clock::time_point m_hashTime = std::chrono::steady_clock::now();
    std::atomic<float> m_hashRate{0.0f};
    uint64_t m_groupCount = 0;
    std::atomic<bool> m_hashRateUpdate{false};
};

}  // namespace eth
}  // namespace dev