#pragma once

#include <stdint.h>
#include <sstream>
#include <stdexcept>
#include <string>

#include "cuda_runtime.h"

/**
 * @brief Constants and structures for Ethash CUDA mining
 *
 * This file contains essential data structures and constants for
 * implementing Ethereum's Ethash algorithm on CUDA GPUs.
 */

/**
 * @brief Maximum number of search results per kernel launch
 *
 * It is virtually impossible to get more than one solution per stream hash calculation
 * Leave room for up to 4 results. A power of 2 here will yield better CUDA optimization
 */
#define MAX_SEARCH_RESULTS 4U

/**
 * @brief Maximum number of iterations to debug
 */
#define DEBUG_MAX_ITERATIONS 5U

/**
 * @brief Number of accesses in the main loop of Ethash algorithm
 */
#define ACCESSES 64

/**
 * @brief Number of threads per hash calculation
 */
#define THREADS_PER_HASH (128 / 16)

/**
 * @brief Structure holding a single search result
 */
struct Search_Result
{
    uint32_t gid;     ///< Global index (contains the nonce)
    uint32_t mix[8];  ///< Mix hash result (8 words)
    uint32_t pad[7];  ///< Padding to make size a power of 2 for optimization
};

/**
 * @brief Container for search results
 */
struct Search_results
{
    Search_Result result[MAX_SEARCH_RESULTS];  ///< Array of found solutions
    uint32_t count = 0;                        ///< Number of valid results
};

/**
 * @brief 32-byte hash structure (used for block header)
 */
typedef struct
{
    uint4 uint4s[32 / sizeof(uint4)];  ///< 32 bytes as uint4 for optimal access
} hash32_t;

/**
 * @brief 128-byte hash structure (used for DAG entries)
 */
typedef union
{
    uint32_t words[128 / sizeof(uint32_t)];  ///< As 32-bit words
    uint2 uint2s[128 / sizeof(uint2)];       ///< As uint2 for vector operations
    uint4 uint4s[128 / sizeof(uint4)];       ///< As uint4 for vector operations
} hash128_t;

/**
 * @brief 64-byte hash structure (used for light cache)
 */
typedef union
{
    uint32_t words[64 / sizeof(uint32_t)];  ///< As 32-bit words
    uint2 uint2s[64 / sizeof(uint2)];       ///< As uint2 for vector operations
    uint4 uint4s[64 / sizeof(uint4)];       ///< As uint4 for vector operations
} hash64_t;

/**
 * @brief Set constants required for Ethash calculations
 *
 * @param _dag Pointer to DAG data
 * @param _dag_size Size of DAG in bytes
 * @param _light Pointer to light cache data
 * @param _light_size Size of light cache in bytes
 */
void set_constants(hash128_t* _dag, uint32_t _dag_size, hash64_t* _light, uint32_t _light_size);

/**
 * @brief Get current constants
 *
 * @param _dag [out] Pointer to DAG data
 * @param _dag_size [out] Size of DAG in bytes
 * @param _light [out] Pointer to light cache data
 * @param _light_size [out] Size of light cache in bytes
 */
void get_constants(hash128_t** _dag, uint32_t* _dag_size, hash64_t** _light, uint32_t* _light_size);

/**
 * @brief Set the block header for mining
 *
 * @param _header Block header data
 */
void set_header(hash32_t _header);

/**
 * @brief Set the target difficulty
 *
 * @param _target Target difficulty as a 64-bit value
 */
void set_target(uint64_t _target);

/**
 * @brief Run the Ethash search algorithm on the GPU
 *
 * @param gridSize CUDA grid size
 * @param blockSize CUDA block size
 * @param stream CUDA stream to use
 * @param g_output Pointer to output buffer for results
 * @param start_nonce Starting nonce value
 */
void run_ethash_search(uint32_t gridSize, uint32_t blockSize, cudaStream_t stream,
    volatile Search_results* g_output, uint64_t start_nonce);

/**
 * @brief Generate the DAG for a specific epoch
 *
 * @param dag_size Size of the DAG to generate
 * @param blocks Number of CUDA blocks to use
 * @param threads Number of threads per block
 * @param stream CUDA stream to use
 */
void ethash_generate_dag(uint64_t dag_size, uint32_t blocks, uint32_t threads, cudaStream_t stream);

/**
 * @brief Exception class for CUDA runtime errors
 */
struct cuda_runtime_error : public virtual std::runtime_error
{
    /**
     * @brief Construct a new cuda runtime error
     *
     * @param msg Error message
     */
    cuda_runtime_error(const std::string& msg) : std::runtime_error(msg) {}
};

/**
 * @brief Macro for safe CUDA API calls
 *
 * Throws a cuda_runtime_error if the CUDA call fails
 */
#define CUDA_SAFE_CALL(call)                                                              \
    do                                                                                    \
    {                                                                                     \
        cudaError_t err = call;                                                           \
        if (cudaSuccess != err)                                                           \
        {                                                                                 \
            std::stringstream ss;                                                         \
            ss << "CUDA error in func " << __FUNCTION__ << " at line " << __LINE__ << ' ' \
               << cudaGetErrorString(err);                                                \
            throw cuda_runtime_error(ss.str());                                           \
        }                                                                                 \
    } while (0)
