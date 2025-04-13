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

#include <libethcore/Farm.h>
#include <ethash/ethash.hpp>

#include "CUDAMiner.h"

using namespace std;
using namespace dev;
using namespace eth;

namespace
{
struct CUDAChannel : public LogChannel
{
    static const char* name() { return EthOrange "cu"; }
    static const int verbosity = 2;
};
}  // namespace

#define cudalog clog(CUDAChannel)

CUDAMiner::CUDAMiner(unsigned _index, CUSettings _settings, DeviceDescriptor& _device)
  : Miner("cuda-", _index),
    m_settings(_settings),
    m_batch_size(_settings.gridSize * _settings.blockSize),
    m_streams_batch_size(_settings.gridSize * _settings.blockSize * _settings.streams)
{
    m_deviceDescriptor = _device;
}

CUDAMiner::~CUDAMiner()
{
    DEV_BUILD_LOG_PROGRAMFLOW(cudalog, "cuda-" << m_index << " CUDAMiner::~CUDAMiner() begin");
    stopWorking();
    kick_miner();
    DEV_BUILD_LOG_PROGRAMFLOW(cudalog, "cuda-" << m_index << " CUDAMiner::~CUDAMiner() end");
}

bool CUDAMiner::initDevice()
{
    cudalog << "Using Pci Id : " << m_deviceDescriptor.uniqueId << " " << m_deviceDescriptor.cuName
            << " (Compute " + m_deviceDescriptor.cuCompute + ") Memory : "
            << dev::getFormattedMemory((double)m_deviceDescriptor.totalMemory);

    // Set Hardware Monitor Info
    m_hwmoninfo.deviceType = HwMonitorInfoType::NVIDIA;
    m_hwmoninfo.devicePciId = m_deviceDescriptor.uniqueId;
    m_hwmoninfo.deviceIndex = -1;  // Will be later on mapped by nvml (see Farm() constructor)

    try
    {
        CUDA_SAFE_CALL(cudaSetDevice(m_deviceDescriptor.cuDeviceIndex));
        CUDA_SAFE_CALL(cudaDeviceReset());
        return true;
    }
    catch (const cuda_runtime_error& ec)
    {
        cudalog << "Could not set CUDA device on Pci Id " << m_deviceDescriptor.uniqueId
                << " Error : " << ec.what();
        cudalog << "Mining aborted on this device.";
        return false;
    }
}

bool CUDAMiner::initEpoch_internal()
{
    // If we get here it means epoch has changed so it's not necessary
    // to check again dag sizes. They're changed for sure
    m_current_target = 0;
    auto startInit = std::chrono::steady_clock::now();

    // Calculate memory requirements
    size_t RequiredTotalMemory = (m_epochContext.dagSize + m_epochContext.lightSize);
    size_t RequiredDagMemory = m_epochContext.dagSize;

    // Release the pause flag if any
    resume(MinerPauseEnum::PauseDueToInsufficientMemory);
    resume(MinerPauseEnum::PauseDueToInitEpochError);

    bool lightOnHost = false;
    try
    {
        hash128_t* dag;
        hash64_t* light;

        // Check if memory reallocation is needed
        bool needMemoryAllocation = (m_allocated_memory_dag < m_epochContext.dagSize ||
                                     m_allocated_memory_light_cache < m_epochContext.lightSize);

        if (needMemoryAllocation)
        {
            // We need to reset the device and (re)create the dag
            CUDA_SAFE_CALL(cudaDeviceReset());
            CUDA_SAFE_CALL(cudaSetDeviceFlags(m_settings.schedule));
            CUDA_SAFE_CALL(cudaDeviceSetCacheConfig(cudaFuncCachePreferL1));

            // Check if device has sufficient memory
            if (m_deviceDescriptor.totalMemory < RequiredTotalMemory)
            {
                if (m_deviceDescriptor.totalMemory < RequiredDagMemory)
                {
                    cudalog << "Epoch " << m_epochContext.epochNumber << " requires "
                            << dev::getFormattedMemory((double)RequiredDagMemory) << " memory.";
                    cudalog << "This device hasn't enough memory available. Mining suspended ...";
                    pause(MinerPauseEnum::PauseDueToInsufficientMemory);
                    return true;  // This will prevent to exit the thread and
                                  // Eventually resume mining when changing coin or epoch (NiceHash)
                }
                else
                {
                    lightOnHost = true;
                }
            }

            cudalog << "Generating DAG + Light(on " << (lightOnHost ? "host" : "GPU")
                    << ") : " << dev::getFormattedMemory((double)RequiredTotalMemory);

            // Allocate memory for light cache
            if (lightOnHost)
            {
                CUDA_SAFE_CALL(cudaHostAlloc(reinterpret_cast<void**>(&light),
                    m_epochContext.lightSize, cudaHostAllocDefault));
                cudalog << "WARNING: Generating DAG will take minutes, not seconds";
            }
            else
            {
                CUDA_SAFE_CALL(
                    cudaMalloc(reinterpret_cast<void**>(&light), m_epochContext.lightSize));
            }

            m_allocated_memory_light_cache = m_epochContext.lightSize;

            // Allocate memory for DAG
            CUDA_SAFE_CALL(cudaMalloc(reinterpret_cast<void**>(&dag), m_epochContext.dagSize));
            m_allocated_memory_dag = m_epochContext.dagSize;

            // Create mining buffers and streams
            for (unsigned i = 0; i != m_settings.streams; ++i)
            {
                CUDA_SAFE_CALL(cudaMallocHost(&m_search_buf[i], sizeof(Search_results)));
                CUDA_SAFE_CALL(cudaStreamCreateWithFlags(&m_streams[i], cudaStreamNonBlocking));
            }
        }
        else
        {
            cudalog << "Generating DAG + Light (reusing buffers): "
                    << dev::getFormattedMemory((double)RequiredTotalMemory);
            get_constants(&dag, NULL, &light, NULL);
        }

        // Copy light cache to device
        CUDA_SAFE_CALL(cudaMemcpy(reinterpret_cast<void*>(light), m_epochContext.lightCache,
            m_epochContext.lightSize, cudaMemcpyHostToDevice));

        // Set constants for the kernel
        set_constants(dag, m_epochContext.dagNumItems, light, m_epochContext.lightNumItems);

        // Generate DAG
        ethash_generate_dag(
            m_epochContext.dagSize, m_settings.gridSize, m_settings.blockSize, m_streams[0]);

        // Log completion information
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startInit)
                            .count();

        size_t remainingMemory = lightOnHost ?
                                     (m_deviceDescriptor.totalMemory - RequiredDagMemory) :
                                     (m_deviceDescriptor.totalMemory - RequiredTotalMemory);

        cudalog << "Generated DAG + Light in " << duration << " ms. "
                << dev::getFormattedMemory((double)remainingMemory) << " left.";

        return true;
    }
    catch (const cuda_runtime_error& ec)
    {
        cudalog << "Unexpected error " << ec.what() << " on CUDA device "
                << m_deviceDescriptor.uniqueId;
        cudalog << "Mining suspended ...";
        pause(MinerPauseEnum::PauseDueToInitEpochError);
        return true;
    }
}

void CUDAMiner::workLoop()
{
    WorkPackage current;
    current.header = h256();

    // Initialize buffers
    m_search_buf.resize(m_settings.streams);
    m_streams.resize(m_settings.streams);

    if (!initDevice())
        return;

    try
    {
        while (!shouldStop())
        {
            // Wait for work or 3 seconds (whichever comes first)
            const WorkPackage w = work();
            if (!w)
            {
                auto const timeout = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                std::unique_lock<std::mutex> l(m_workMutex);
                m_new_work_signal.wait_until(l, timeout);
                continue;
            }

            // Handle epoch change
            if (current.epoch != w.epoch)
            {
                if (!initEpoch())
                    break;  // Exit thread if epoch initialization fails

                // Get latest job after epoch change
                current = w;
                continue;
            }

            // Update current work package
            current = w;
            uint64_t upper64OfBoundary =
                static_cast<uint64_t>(static_cast<u64>((u256)current.boundary >> 192));

            // Start searching
            search(current.header.data(), upper64OfBoundary, current.startNonce, w);
        }

        // Clean up resources when stopping
        CUDA_SAFE_CALL(cudaDeviceReset());
    }
    catch (cuda_runtime_error const& _e)
    {
        string _what = "GPU error: ";
        _what.append(_e.what());
        throw std::runtime_error(_what);
    }
}

void CUDAMiner::kick_miner()
{
    m_new_work.store(true, std::memory_order_relaxed);
    m_new_work_signal.notify_one();
}

int CUDAMiner::getNumDevices()
{
    int deviceCount;
    cudaError_t err = cudaGetDeviceCount(&deviceCount);

    if (err == cudaSuccess)
        return deviceCount;

    if (err == cudaErrorInsufficientDriver)
    {
        int driverVersion = 0;
        cudaDriverGetVersion(&driverVersion);
        if (driverVersion == 0)
            std::cerr << "CUDA Error: No CUDA driver found" << std::endl;
        else
            std::cerr << "CUDA Error: Insufficient CUDA driver " << std::to_string(driverVersion)
                      << std::endl;
    }
    else
    {
        std::cerr << "CUDA Error: " << cudaGetErrorString(err) << std::endl;
    }

    return 0;
}

void CUDAMiner::enumDevices(std::map<string, DeviceDescriptor>& _DevicesCollection)
{
    int numDevices = getNumDevices();

    for (int i = 0; i < numDevices; i++)
    {
        try
        {
            // Get device properties
            cudaDeviceProp props;
            CUDA_SAFE_CALL(cudaGetDeviceProperties(&props, i));

            // Get memory info
            size_t freeMem, totalMem;
            CUDA_SAFE_CALL(cudaMemGetInfo(&freeMem, &totalMem));

            // Create unique ID based on PCI bus
            ostringstream s;
            s << setw(2) << setfill('0') << hex << props.pciBusID << ":" << setw(2)
              << props.pciDeviceID << ".0";
            string uniqueId = s.str();

            // Initialize or update device descriptor
            DeviceDescriptor deviceDescriptor =
                (_DevicesCollection.find(uniqueId) != _DevicesCollection.end()) ?
                    _DevicesCollection[uniqueId] :
                    DeviceDescriptor();

            // Update device properties
            deviceDescriptor.name = string(props.name);
            deviceDescriptor.cuDetected = true;
            deviceDescriptor.uniqueId = uniqueId;
            deviceDescriptor.type = DeviceTypeEnum::Gpu;
            deviceDescriptor.cuDeviceIndex = i;
            deviceDescriptor.cuDeviceOrdinal = i;
            deviceDescriptor.cuName = string(props.name);
            deviceDescriptor.totalMemory = freeMem;
            deviceDescriptor.cuCompute = (to_string(props.major) + "." + to_string(props.minor));
            deviceDescriptor.cuComputeMajor = props.major;
            deviceDescriptor.cuComputeMinor = props.minor;

            // Store updated descriptor
            _DevicesCollection[uniqueId] = deviceDescriptor;
        }
        catch (const cuda_runtime_error& _e)
        {
            std::cerr << _e.what() << std::endl;
        }
    }
}

void CUDAMiner::search(
    uint8_t const* header, uint64_t target, uint64_t start_nonce, const dev::eth::WorkPackage& w)
{
    // Set header and target for the kernel
    set_header(*reinterpret_cast<hash32_t const*>(header));
    if (m_current_target != target)
    {
        set_target(target);
        m_current_target = target;
    }

    // Ensure all previous kernels are complete
    CUDA_SAFE_CALL(cudaDeviceSynchronize());

    // Prime each stream, clear search result buffers and start the search
    uint32_t current_index;
    for (current_index = 0; current_index < m_settings.streams;
        current_index++, start_nonce += m_batch_size)
    {
        cudaStream_t stream = m_streams[current_index];
        volatile Search_results& buffer(*m_search_buf[current_index]);
        buffer.count = 0;

        // Run the batch for this stream
        run_ethash_search(m_settings.gridSize, m_settings.blockSize, stream, &buffer, start_nonce);
    }

    // Process stream batches until we get new work
    bool done = false;

    while (!done)
    {
        // Check for new work or pause
        bool workFlag = true;
        done = m_new_work.compare_exchange_strong(workFlag, false) || paused();

        // Process each stream
        for (current_index = 0; current_index < m_settings.streams;
            current_index++, start_nonce += m_batch_size)
        {
            if (shouldStop())
            {
                m_new_work.store(false, std::memory_order_relaxed);
                done = true;
                break;  // Exit loop immediately
            }

            // Get stream and synchronize
            cudaStream_t stream = m_streams[current_index];
            CUDA_SAFE_CALL(cudaStreamSynchronize(stream));

            // Process any solutions found
            volatile Search_results& buffer(*m_search_buf[current_index]);
            uint32_t found_count = std::min((unsigned)buffer.count, MAX_SEARCH_RESULTS);

            if (found_count)
            {
                // Submit found solutions
                uint64_t nonce_base = start_nonce - m_streams_batch_size;
                for (uint32_t i = 0; i < found_count; i++)
                {
                    uint32_t gid = buffer.result[i].gid;
                    h256 mix;
                    memcpy(mix.data(), (void*)&buffer.result[i].mix, sizeof(buffer.result[i].mix));

                    uint64_t nonce = nonce_base + gid;
                    Farm::f().submitProof(
                        Solution{nonce, mix, w, std::chrono::steady_clock::now(), m_index});

                    cudalog << EthWhite << "Job: " << w.header.abridged() << " Sol: 0x"
                            << toHex(nonce) << EthReset;
                }

                // Reset buffer counter
                buffer.count = 0;
            }

            // Start next batch if not done
            if (!done)
            {
                run_ethash_search(
                    m_settings.gridSize, m_settings.blockSize, stream, &buffer, start_nonce);
            }
        }

        // Update hash rate statistics
        updateHashRate(m_batch_size, m_settings.streams);

        // Check for stop signal
        if (shouldStop())
        {
            m_new_work.store(false, std::memory_order_relaxed);
            break;
        }
    }

#ifdef DEV_BUILD
    // Log job switch time if enabled
    if (!shouldStop() && (g_logOptions & LOG_SWITCH))
    {
        auto switchTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_workSwitchStart)
                              .count();
        cudalog << "Switch time: " << switchTime << " ms.";
    }
#endif
}
