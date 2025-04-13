/**
 * @file fnv.cuh
 * @brief FNV hash implementation for CUDA
 *
 * This file provides device functions for calculating FNV hash values
 * using an implementation that matches the Go reference.
 */

namespace
{
// FNV prime constant used in the FNV-1a hash
constexpr uint32_t FNV_PRIME = 0x01000193U;
}  // namespace

/**
 * @brief Optimized FNV hash for a single element (optimized version)
 * @param a Input value to be multiplied by FNV_PRIME
 * @param b Value to be XORed with the result
 * @return Result of FNV hash
 */
__device__ __forceinline__ uint32_t fnv(uint32_t a, uint32_t b)
{
// FNV_PRIME est typiquement 0x01000193 (16777619 en décimal)
#define FNV_PRIME 0x01000193U

    uint32_t result;

#if __CUDA_ARCH__ >= 500
    // Version avec instruction PTX pour GPU récents
    asm volatile(
        "mul.lo.u32 %0, %1, %2;\n\t"
        "xor.b32 %0, %0, %3;\n\t"
        : "=r"(result)
        : "r"(a), "r"(FNV_PRIME), "r"(b));
#else
    // Multiplication standard suivie d'un XOR
    result = a * FNV_PRIME ^ b;
#endif

    return result;
}

/**
 * @brief Performs FNV hash on arrays of values
 *
 * Applies the FNV hash to each element in the mix array using
 * the corresponding element from the data array. This function
 * exactly matches the Go reference implementation.
 *
 * @param mix Array to be updated with hash results (8 elements)
 * @param data Input data array (8 elements)
 */
__device__ void fnv_hash(uint32_t* mix, const uint32_t* data)
{
#pragma unroll
    for (int i = 0; i < 8; i++)
    {
        mix[i] = mix[i] * FNV_PRIME ^ data[i];
    }
}