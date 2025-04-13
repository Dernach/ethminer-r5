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

#include "ethash_cuda_miner_kernel.h"

#include <libdevcore/Worker.h>
#include <libethcore/EthashAux.h>
#include <libethcore/Miner.h>

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace dev
{
namespace eth
{

using std::string;

/**
 * @brief CUDA implementation of Ethash miner
 *
 * This class provides GPU mining capabilities using CUDA for Ethereum's
 * Ethash Proof of Work algorithm.
 */
class CUDAMiner : public Miner
{
public:
    /**
     * @brief Construct a new CUDA miner
     *
     * @param _index Device index
     * @param _settings CUDA settings for the miner
     * @param _device Device descriptor for the GPU to use
     */
    CUDAMiner(unsigned _index, CUSettings _settings, DeviceDescriptor& _device);

    /**
     * @brief Destructor, ensures proper resource cleanup
     */
    ~CUDAMiner() override;

    /**
     * @brief Get the number of available CUDA devices
     *
     * @return int Number of CUDA-capable devices
     */
    static int getNumDevices();

    /**
     * @brief Enumerate all CUDA devices and populate the devices collection
     *
     * @param _DevicesCollection Map to be populated with device information
     */
    static void enumDevices(std::map<string, DeviceDescriptor>& _DevicesCollection);

    /**
     * @brief Search for a solution meeting the target difficulty
     *
     * @param header Block header to hash
     * @param target Target difficulty to meet
     * @param _startN Starting nonce
     * @param w Work package information
     */
    void search(
        uint8_t const* header, uint64_t target, uint64_t _startN, const dev::eth::WorkPackage& w);

protected:
    /**
     * @brief Initialize the CUDA device
     *
     * @return true if device was successfully initialized
     * @return false if there was an error during initialization
     */
    bool initDevice() override;

    /**
     * @brief Initialize epoch data for the current mining epoch
     *
     * @return true if epoch data was successfully loaded
     * @return false if there was an error during initialization
     */
    bool initEpoch_internal() override;

    /**
     * @brief Signal the miner to process new work
     */
    void kick_miner() override;

private:
    /**
     * @brief Main work loop for the miner
     *
     * This method continuously processes mining work until stopped.
     */
    void workLoop() override;

    // Member variables
    std::atomic<bool> m_new_work{false};  ///< Flag to indicate that new work is available
    std::vector<volatile Search_results*> m_search_buf;  ///< Search result buffers for each stream
    std::vector<cudaStream_t> m_streams;                 ///< CUDA streams for parallel execution
    uint64_t m_current_target = 0;                       ///< Current target difficulty
    CUSettings m_settings;                               ///< CUDA-specific settings for this miner
    const uint32_t m_batch_size;                         ///< Batch size for mining operations
    const uint32_t m_streams_batch_size;                 ///< Batch size per stream
    uint64_t m_allocated_memory_dag = 0;  ///< Memory allocated for directed acyclic graph (DAG)
    size_t m_allocated_memory_light_cache = 0;  ///< Memory allocated for light cache
};

}  // namespace eth
}  // namespace dev
