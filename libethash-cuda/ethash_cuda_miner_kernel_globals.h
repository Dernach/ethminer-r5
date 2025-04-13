#pragma once

/**
 * @file constants.cuh
 * @brief CUDA constant memory definitions and platform-specific macros for Ethash algorithm
 *
 * This file defines GPU constant memory variables and platform-specific optimizations
 * used in the Ethash CUDA implementation.
 */

/**
 * @brief DAG size in bytes (stored in constant memory)
 */
__constant__ uint32_t d_dag_size;

/**
 * @brief Pointer to DAG data in device memory (stored in constant memory)
 */
__constant__ hash64_t* d_dag;

/**
 * @brief Light cache size in bytes (stored in constant memory)
 */
__constant__ uint32_t d_light_size;

/**
 * @brief Pointer to light cache data in device memory (stored in constant memory)
 */
__constant__ uint32_t* d_light;

/**
 * @brief Block header to mine (stored in constant memory)
 */
__constant__ hash32_t d_header;

/**
 * @brief Target difficulty value (stored in constant memory)
 */
__constant__ uint64_t d_target;

/**
 * @brief Optimized warp shuffle operation
 *
 * Provides compatibility across different CUDA architectures by selecting
 * the appropriate shuffle implementation based on the CUDA version.
 *
 * @param x Value to shuffle
 * @param y Source lane
 * @param z Width of the shuffle
 * @return The shuffled value
 */
#if (__CUDACC_VER_MAJOR__ > 8)
#define SHFL(x, y, z) __shfl_sync(0xFFFFFFFF, (x), (y), (z))
#else
#define SHFL(x, y, z) __shfl((x), (y), (z))
#endif

/**
 * @brief Optimized memory load operation
 *
 * Uses texture cache for memory loads on supported architectures to improve
 * performance and reduce impact of memory latency.
 *
 * @param x Reference to the value to load
 * @return The loaded value
 */
#if (__CUDA_ARCH__ >= 320)
#define LDG(x) __ldg(&(x))
#else
#define LDG(x) (x)
#endif
