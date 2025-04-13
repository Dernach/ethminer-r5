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
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/dll.hpp>
#include <boost/filesystem.hpp>
#include <boost/process.hpp>

#include <json/json.h>

#include <libdevcore/Common.h>
#include <libdevcore/Worker.h>

#include <libethcore/Miner.h>

#include <libhwmon/wrapnvml.h>
#if defined(__linux)
#include <libhwmon/wrapamdsysfs.h>
#include <sys/stat.h>
#else
#include <libhwmon/wrapadl.h>
#endif

// Forward declaration of global variable
extern boost::asio::io_service g_io_service;

namespace dev
{
namespace eth
{

/**
 * @brief Farm settings structure
 */
struct FarmSettings
{
    unsigned dagLoadMode = 0;  // 0 = Parallel; 1 = Serialized
    bool noEval = false;       // Whether or not to re-evaluate solutions
    unsigned hwMon = 0;        // 0 - No monitor; 1 - Temp and Fan; 2 - Temp Fan Power
    unsigned ergodicity = 0;   // 0=default, 1=per session, 2=per job
    unsigned tempStart = 40;   // Temperature threshold to restart mining (if paused)
    unsigned tempStop = 0;     // Temperature threshold to pause mining (overheating)
};

/**
 * @brief A collective of Miners.
 * Miners ask for work, then submit proofs
 * @threadsafe
 */
class Farm : public FarmFace
{
public:
    /**
     * @brief Farm constructor
     * @param _DevicesCollection Collection of available devices
     * @param _settings Farm settings
     * @param _CUSettings CUDA settings
     * @param _CLSettings OpenCL settings
     * @param _CPSettings CPU settings
     */
    Farm(std::map<std::string, DeviceDescriptor>& _DevicesCollection, FarmSettings _settings,
        CUSettings _CUSettings, CLSettings _CLSettings, CPSettings _CPSettings);

    /**
     * @brief Farm destructor
     */
    ~Farm();

    /**
     * @brief Get singleton instance
     * @return Reference to the Farm singleton
     */
    static Farm& f() { return *m_this; }

    /**
     * @brief Randomizes the nonce scrambler
     */
    void shuffle();

    /**
     * @brief Sets the current mining mission.
     * @param _newWp The work package we wish to be mining.
     */
    void setWork(const WorkPackage& _newWp);

    /**
     * @brief Start a number of miners.
     * @return true if miners were successfully started
     */
    bool start();

    /**
     * @brief Stop all mining activities.
     * Implies all mining threads are stopped.
     */
    void stop();

    /**
     * @brief Signals all miners to suspend mining
     */
    void pause();

    /**
     * @brief Whether or not the whole farm has been paused
     * @return true if farm is paused
     */
    bool paused();

    /**
     * @brief Signals all miners to resume mining
     */
    void resume();

    /**
     * @brief Stop all mining activities and start them again
     */
    void restart();

    /**
     * @brief Stop all mining activities and start them again (async post)
     */
    void restart_async();

    /**
     * @brief Returns whether or not the farm has been started
     * @return true if mining is active
     */
    bool isMining() const { return m_isMining.load(std::memory_order_relaxed); }

    /**
     * @brief Spawn a reboot script (reboot.bat/reboot.sh)
     * @param args Arguments to pass to the reboot script
     * @return false if no matching file was found
     */
    bool reboot(const std::vector<std::string>& args);

    /**
     * @brief Get information on the progress of mining this work package.
     * @return The progress with mining so far.
     */
    TelemetryType& Telemetry() { return m_telemetry; }

    /**
     * @brief Gets current hashrate
     * @return Current hashrate in H/s
     */
    float HashRate() const { return m_telemetry.farm.hashrate; }

    /**
     * @brief Gets the collection of pointers to miner instances
     * @return Vector of miner shared pointers
     */
    std::vector<std::shared_ptr<Miner>> getMiners() const { return m_miners; }

    /**
     * @brief Gets the number of miner instances
     * @return Count of miners
     */
    unsigned getMinersCount() const { return static_cast<unsigned>(m_miners.size()); }

    /**
     * @brief Gets the pointer to a miner instance
     * @param index Miner index
     * @return Pointer to miner or nullptr if index is invalid
     */
    std::shared_ptr<Miner> getMiner(unsigned index) const
    {
        if (index < m_miners.size())
        {
            return m_miners[index];
        }
        return nullptr;
    }

    /**
     * @brief Accounts a solution to a miner and, as a consequence, to
     *  the whole farm
     * @param _minerIdx Miner index
     * @param _accounting Type of accounting to perform
     */
    void accountSolution(unsigned _minerIdx, SolutionAccountingEnum _accounting) override;

    /**
     * @brief Gets the solutions account for the whole farm
     * @return Solution accounting data
     */
    SolutionAccountType getSolutions() const;

    /**
     * @brief Gets the solutions account for single miner
     * @param _minerIdx Miner index
     * @return Solution accounting data for specified miner
     */
    SolutionAccountType getSolutions(unsigned _minerIdx) const;

    // Function type definitions
    using SolutionFound = std::function<void(const Solution&)>;
    using MinerRestart = std::function<void()>;

    /**
     * @brief Set callback for solution found events
     * @param _handler Function to be called when a solution is found
     */
    void onSolutionFound(const SolutionFound& _handler) { m_onSolutionFound = _handler; }

    /**
     * @brief Set callback for miner restart events
     * @param _handler Function to be called when a miner needs to restart
     */
    void onMinerRestart(const MinerRestart& _handler) { m_onMinerRestart = _handler; }

    /**
     * @brief Gets the actual start nonce of the segment picked by the farm
     * @return Nonce scrambler value
     */
    uint64_t get_nonce_scrambler() override { return m_nonce_scrambler; }

    /**
     * @brief Gets the actual width of each subsegment assigned to miners
     * @return Segment width
     */
    unsigned get_segment_width() override { return m_nonce_segment_width; }

    /**
     * @brief Sets the actual start nonce of the segment picked by the farm
     * @param n New nonce scrambler value
     */
    void set_nonce_scrambler(uint64_t n) { m_nonce_scrambler = n; }

    /**
     * @brief Sets the actual width of each subsegment assigned to miners
     * @param n New segment width
     */
    void set_nonce_segment_width(unsigned n)
    {
        if (!m_currentWp.exSizeBytes)
        {
            m_nonce_segment_width = n;
        }
    }

    /**
     * @brief Provides the description of segments each miner is working on
     * @return a JsonObject containing segment information
     */
    Json::Value get_nonce_scrambler_json();

    /**
     * @brief Set temperature thresholds
     * @param tstart Temperature to resume mining
     * @param tstop Temperature to pause mining
     */
    void setTStartTStop(unsigned tstart, unsigned tstop);

    /**
     * @brief Get start temperature threshold
     * @return Temperature at which to resume mining
     */
    unsigned get_tstart() override { return m_Settings.tempStart; }

    /**
     * @brief Get stop temperature threshold
     * @return Temperature at which to pause mining
     */
    unsigned get_tstop() override { return m_Settings.tempStop; }

    /**
     * @brief Get ergodicity setting
     * @return Current ergodicity value
     */
    unsigned get_ergodicity() override { return m_Settings.ergodicity; }

    /**
     * @brief Called from a Miner to note a WorkPackage has a solution.
     * @param _s The solution.
     */
    void submitProof(const Solution& _s) override;

    // Temperature thresholds
    unsigned tstart = 0;
    unsigned tstop = 0;

private:
    /**
     * @brief Async submits solution serializing execution in Farm's strand
     * @param _s The solution
     */
    void submitProofAsync(const Solution& _s);

    /**
     * @brief Collects data about hashing and hardware status
     * @param ec Error code
     */
    void collectData(const boost::system::error_code& ec);

    /**
     * @brief Spawn a file - must be located in the directory of ethminer binary
     * @param filename Name of the file to execute
     * @param args Arguments to pass to the executable
     * @return false if file was not found or it is not executable
     */
    bool spawn_file_in_bin_dir(const char* filename, const std::vector<std::string>& args);

    // Data members
    mutable Mutex x_minerWork;
    std::vector<std::shared_ptr<Miner>> m_miners;  // Collection of miners

    WorkPackage m_currentWp;
    EpochContext m_currentEc;

    std::atomic<bool> m_isMining{false};
    std::atomic<bool> m_paused{false};

    TelemetryType m_telemetry;  // Holds progress and status info for farm and miners

    SolutionFound m_onSolutionFound;
    MinerRestart m_onMinerRestart;

    FarmSettings m_Settings;  // Own Farm Settings
    CUSettings m_CUSettings;  // Cuda settings passed to CUDA Miner instantiator
    CLSettings m_CLSettings;  // OpenCL settings passed to CL Miner instantiator
    CPSettings m_CPSettings;  // CPU settings passed to CPU Miner instantiator

    boost::asio::io_service::strand m_io_strand;
    boost::asio::deadline_timer m_collectTimer;
    static constexpr int m_collectInterval = 5000;

    std::string m_pool_addresses;

    // StartNonce (non-NiceHash Mode) and
    // segment width assigned to each GPU as exponent of 2
    // considering an average block time of 15 seconds
    // a single device GPU should need a speed of 286 Mh/s
    // before it consumes the whole 2^32 segment
    uint64_t m_nonce_scrambler;
    unsigned int m_nonce_segment_width = 32;

    // Hardware monitoring wrappers and mappers
    wrap_nvml_handle* nvmlh = nullptr;
    std::map<std::string, int> map_nvml_handle{};

#if defined(__linux)
    wrap_amdsysfs_handle* sysfsh = nullptr;
    std::map<std::string, int> map_amdsysfs_handle{};
#else
    wrap_adl_handle* adlh = nullptr;
    std::map<std::string, int> map_adl_handle{};
#endif

    static Farm* m_this;
    std::map<std::string, DeviceDescriptor>& m_DevicesCollection;
};

}  // namespace eth
}  // namespace dev
