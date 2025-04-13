#include "cuda_helper.h"
#include "ethash_cuda_miner_kernel.h"
#include "ethash_cuda_miner_kernel_globals.h"
#include "fnv.cuh"
#include "keccak.cuh"
#include "dagger_shuffled.cuh"

namespace
{
/**
 * @brief Helper function to copy array contents
 * @tparam T Data type of the arrays
 * @param dst Destination array
 * @param src Source array
 * @param count Number of elements to copy
 */
template <typename T>
__device__ __host__ inline void copy_array(T* dst, const T* src, int count)
{
#pragma unroll 8
    for (int i = 0; i < count; ++i)
    {
        dst[i] = src[i];
    }
}

// Algorithm constants
constexpr uint32_t ETHASH_DATASET_PARENTS = 256;
constexpr uint32_t NODE_WORDS = (64 / 4);
constexpr uint32_t CACHE_ITEM_WORDS = 16;  // 64 bytes = 16 words of 32 bits
constexpr uint32_t DAG_ITEM_WORDS = 16;    // 64 bytes = 16 words of 32 bits
}  // namespace

/**
 * @brief Optimized kernel for Ethash search
 */
__global__ void ethash_search(volatile Search_results* g_output, uint64_t start_nonce)
{
    uint32_t const gid = blockIdx.x * blockDim.x + threadIdx.x;
    uint2 mix[4];

    bool found_solution = compute_hash(start_nonce + gid, mix);
    if (!found_solution)
        return;

    // Optimized atomic addition to results buffer
    uint32_t index = atomicInc((uint32_t*)&g_output->count, 0xffffffff);
    bool can_store = (index < MAX_SEARCH_RESULTS);

    // Store result details with optimized conditional operations
    g_output->result[index].gid = can_store ? gid : g_output->result[index].gid;

    // Vectorized storage of mix hash
#pragma unroll
    for (int i = 0; i < 8; i++)
    {
        g_output->result[index].mix[i] = can_store ? ((i % 2 == 0) ? mix[i / 2].x : mix[i / 2].y) :
                                                     g_output->result[index].mix[i];
    }
}

/**
 * @brief Launches the ethash search kernel
 * @param gridSize Number of thread blocks
 * @param blockSize Number of threads per block
 * @param stream CUDA stream to use
 * @param g_output Output buffer for search results
 * @param start_nonce Starting nonce value
 */
void run_ethash_search(uint32_t gridSize, uint32_t blockSize, cudaStream_t stream,
    volatile Search_results* g_output, uint64_t start_nonce)
{
    ethash_search<<<gridSize, blockSize, 0, stream>>>(g_output, start_nonce);
    CUDA_SAFE_CALL(cudaGetLastError());
}

/**
 * @brief Kernel to calculate DAG items with optimized memory access
 */
__global__ void ethash_calculate_dag_item(uint32_t start) {
    // Optimized node index calculation
    uint32_t const node_index = start + blockIdx.x * blockDim.x + threadIdx.x;

    // Early exit to avoid warp divergence
    if (((node_index >> 1) & (~1)) >= d_dag_size)
        return;

    // Unified structure for DAG node calculation
    union {
        hash128_t dag_node;
        uint2 dag_node_mem[25];
    };

    // Efficient vectorized initialization
    uint2 zero = make_uint2(0, 0);
#pragma unroll
    for (int i = 0; i < 25; i++) {
        dag_node_mem[i] = zero;
    }

    // Optimized index calculations and vectorized loads
    uint32_t const cache_index = (node_index % d_light_size) * CACHE_ITEM_WORDS;
    uint4* light_vec = (uint4*)&d_light[cache_index];
    uint4* node_vec = (uint4*)&dag_node.words[0];

    // Coalesced memory reads
#pragma unroll
    for (int i = 0; i < 4; i++) {
        node_vec[i] = __ldg(light_vec + i);
    }

    // Direct XOR in register
    dag_node.words[0] ^= node_index;

    // Initial SHA3 calculation
    SHA3_512(dag_node_mem);

    // Parent calculation optimized for cache and memory latency
#pragma unroll 2
    for (uint32_t i = 0; i < ETHASH_DATASET_PARENTS; ++i) {
        // Precalculate indices to exploit ILP
        uint32_t mix = fnv(node_index ^ i, dag_node.words[i % NODE_WORDS]);
        uint32_t parent_index = mix % d_light_size;
        uint32_t cache_parent_index = parent_index * CACHE_ITEM_WORDS;

        // Vectorized accesses with L1 cache hint
        uint4* parent_vec = (uint4*)&d_light[cache_parent_index];

#pragma unroll
        for (int j = 0; j < 4; j++) {
            uint4 parent_data = __ldg(parent_vec + j);

            // Parallelized FNV application
            uint4 result;
            result.x = fnv(dag_node.words[j * 4], parent_data.x);
            result.y = fnv(dag_node.words[j * 4 + 1], parent_data.y);
            result.z = fnv(dag_node.words[j * 4 + 2], parent_data.z);
            result.w = fnv(dag_node.words[j * 4 + 3], parent_data.w);

            node_vec[j] = result;
        }
    }

    // Final SHA3 hash
    SHA3_512(dag_node_mem);

    // Optimized vectorized write to DAG
    hash64_t* dag_nodes = (hash64_t*)d_dag;
    uint4* dag_output = (uint4*)&dag_nodes[node_index].words[0];

#pragma unroll
    for (int i = 0; i < 4; i++) {
        dag_output[i] = node_vec[i];
    }
}

/**
 * @brief Generates the DAG on the GPU with improved synchronization
 */
void ethash_generate_dag(
    uint64_t dag_size, uint32_t gridSize, uint32_t blockSize, cudaStream_t stream) {
    const uint32_t work = (uint32_t)(dag_size / sizeof(hash64_t));
    const uint32_t run = gridSize * blockSize;

    // Optimization: efficient batching with multi-streams
    cudaStream_t streams[2];
    for (int i = 0; i < 2; i++) {
        cudaStreamCreate(&streams[i]);
    }

    // Alternating batch processing between streams
    int stream_idx = 0;
    uint32_t base = 0;

    while (base < work) {
        uint32_t batch_size = min(run, work - base);
        uint32_t grid = (batch_size + blockSize - 1) / blockSize;

        ethash_calculate_dag_item<<<grid, blockSize, 0, streams[stream_idx]>>>(base);

        base += batch_size;
        stream_idx = (stream_idx + 1) % 2;
    }

    // Final synchronization of all streams
    for (int i = 0; i < 2; i++) {
        cudaStreamSynchronize(streams[i]);
        cudaStreamDestroy(streams[i]);
    }

    CUDA_SAFE_CALL(cudaGetLastError());
}

/**
 * @brief Sets the constant memory values for DAG and light cache
 * @param _dag Pointer to DAG memory
 * @param _dag_size Size of the DAG
 * @param _light Pointer to light cache memory
 * @param _light_size Size of the light cache
 */
void set_constants(hash128_t* _dag, uint32_t _dag_size, hash64_t* _light, uint32_t _light_size)
{
    CUDA_SAFE_CALL(cudaMemcpyToSymbol(d_dag, &_dag, sizeof(hash128_t*)));
    CUDA_SAFE_CALL(cudaMemcpyToSymbol(d_dag_size, &_dag_size, sizeof(uint32_t)));
    CUDA_SAFE_CALL(cudaMemcpyToSymbol(d_light, &_light, sizeof(hash64_t*)));
    CUDA_SAFE_CALL(cudaMemcpyToSymbol(d_light_size, &_light_size, sizeof(uint32_t)));
}

/**
 * @brief Gets the constant memory values for DAG and light cache
 * @param _dag Pointer to receive DAG memory pointer
 * @param _dag_size Pointer to receive DAG size
 * @param _light Pointer to receive light cache memory pointer
 * @param _light_size Pointer to receive light cache size
 */
void get_constants(hash128_t** _dag, uint32_t* _dag_size, hash64_t** _light, uint32_t* _light_size)
{
    // Use conditional checks to handle nullptr parameters
    if (_dag)
    {
        hash128_t* _d;
        CUDA_SAFE_CALL(cudaMemcpyFromSymbol(&_d, d_dag, sizeof(hash128_t*)));
        *_dag = _d;
    }

    if (_dag_size)
    {
        uint32_t _ds;
        CUDA_SAFE_CALL(cudaMemcpyFromSymbol(&_ds, d_dag_size, sizeof(uint32_t)));
        *_dag_size = _ds;
    }

    if (_light)
    {
        hash64_t* _l;
        CUDA_SAFE_CALL(cudaMemcpyFromSymbol(&_l, d_light, sizeof(hash64_t*)));
        *_light = _l;
    }

    if (_light_size)
    {
        uint32_t _ls;
        CUDA_SAFE_CALL(cudaMemcpyFromSymbol(&_ls, d_light_size, sizeof(uint32_t)));
        *_light_size = _ls;
    }
}

/**
 * @brief Sets the header hash in constant memory
 * @param _header Header hash
 */
void set_header(hash32_t _header)
{
    CUDA_SAFE_CALL(cudaMemcpyToSymbol(d_header, &_header, sizeof(hash32_t)));
}

/**
 * @brief Sets the target difficulty in constant memory
 * @param _target Target difficulty
 */
void set_target(uint64_t _target)
{
    CUDA_SAFE_CALL(cudaMemcpyToSymbol(d_target, &_target, sizeof(uint64_t)));
}
