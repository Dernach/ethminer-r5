#include "cuda_helper.h"

/**
 * @brief Keccak round constants for the permutation function
 * These constants are used in the iota step of each round
 */
__device__ __constant__ uint2 const keccak_round_constants[24] = {{0x00000001, 0x00000000},
    {0x00008082, 0x00000000}, {0x0000808a, 0x80000000}, {0x80008000, 0x80000000},
    {0x0000808b, 0x00000000}, {0x80000001, 0x00000000}, {0x80008081, 0x80000000},
    {0x00008009, 0x80000000}, {0x0000008a, 0x00000000}, {0x00000088, 0x00000000},
    {0x80008009, 0x00000000}, {0x8000000a, 0x00000000}, {0x8000808b, 0x00000000},
    {0x0000008b, 0x80000000}, {0x00008089, 0x80000000}, {0x00008003, 0x80000000},
    {0x00008002, 0x80000000}, {0x00000080, 0x80000000}, {0x0000800a, 0x00000000},
    {0x8000000a, 0x80000000}, {0x80008081, 0x80000000}, {0x00008080, 0x80000000},
    {0x80000001, 0x00000000}, {0x80008008, 0x80000000}};

/**
 * @brief Rotation constants for Keccak-f permutation
 * Used in the rho step to rotate lanes
 */
__device__ __constant__ uint64_t keccakf_rotc[24] = {
    1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14, 27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44};

/**
 * @brief Permutation indices for the pi step of Keccak-f
 * These indices define the rearrangement of lanes
 */
__device__ __constant__ uint64_t keccakf_piln[24] = {
    10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4, 15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1};

/**
 * @brief Round constants for the Keccak-f permutation
 * Used in the iota step of each round
 */
__device__ __constant__ uint64_t keccakf_rndc[24] = {0x0000000000000001ULL, 0x0000000000008082ULL,
    0x800000000000808aULL, 0x8000000080008000ULL, 0x000000000000808bULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL, 0x000000000000008aULL, 0x0000000000000088ULL,
    0x0000000080008009ULL, 0x000000008000000aULL, 0x000000008000808bULL, 0x800000000000008bULL,
    0x8000000000008089ULL, 0x8000000000008003ULL, 0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800aULL, 0x800000008000000aULL, 0x8000000080008081ULL, 0x8000000000008080ULL,
    0x0000000080000001ULL, 0x8000000080008008ULL};

/**
 * @brief Performs XOR of 5 uint2 values with hardware acceleration when available
 *
 * @param a First uint2 value
 * @param b Second uint2 value
 * @param c Third uint2 value
 * @param d Fourth uint2 value
 * @param e Fifth uint2 value
 * @return XOR of all input values
 */
DEV_INLINE uint2 xor5(const uint2 a, const uint2 b, const uint2 c, const uint2 d, const uint2 e)
{
#if __CUDA_ARCH__ >= 500 && CUDA_VERSION >= 7050
    // Hardware-accelerated implementation using LOP3 instruction
    // LOP3 0x96 corresponds to XOR of three operands
    uint2 result;
    asm volatile(
        "// xor5\n\t"
        "lop3.b32 %0, %2, %3, %4, 0x96;\n\t"  // result.x = a.x ^ b.x ^ c.x
        "lop3.b32 %0, %0, %5, %6, 0x96;\n\t"  // result.x ^= d.x ^ e.x
        "lop3.b32 %1, %7, %8, %9, 0x96;\n\t"  // result.y = a.y ^ b.y ^ c.y
        "lop3.b32 %1, %1, %10, %11, 0x96;"    // result.y ^= d.y ^ e.y
        : "=r"(result.x), "=r"(result.y)
        : "r"(a.x), "r"(b.x), "r"(c.x), "r"(d.x), "r"(e.x), "r"(a.y), "r"(b.y), "r"(c.y), "r"(d.y),
        "r"(e.y));
    return result;
#else
    // Fallback implementation for older architectures
    return a ^ b ^ c ^ d ^ e;
#endif
}

/**
 * @brief Performs XOR of 3 uint2 values with hardware acceleration when available
 *
 * @param a First uint2 value
 * @param b Second uint2 value
 * @param c Third uint2 value
 * @return XOR of all input values
 */
DEV_INLINE uint2 xor3(const uint2 a, const uint2 b, const uint2 c)
{
#if __CUDA_ARCH__ >= 500 && CUDA_VERSION >= 7050
    // Hardware-accelerated implementation using LOP3 instruction
    uint2 result;
    asm volatile(
        "// xor3\n\t"
        "lop3.b32 %0, %2, %3, %4, 0x96;\n\t"  // result.x = a.x ^ b.x ^ c.x
        "lop3.b32 %1, %5, %6, %7, 0x96;"      // result.y = a.y ^ b.y ^ c.y
        : "=r"(result.x), "=r"(result.y)
        : "r"(a.x), "r"(b.x), "r"(c.x), "r"(a.y), "r"(b.y), "r"(c.y));
    return result;
#else
    // Fallback implementation for older architectures
    return a ^ b ^ c;
#endif
}

/**
 * @brief Implements the chi step of Keccak-f permutation
 * The chi step provides nonlinearity to the permutation
 *
 * @param a First uint2 value
 * @param b Second uint2 value
 * @param c Third uint2 value
 * @return Result of the chi operation: a ^ ((~b) & c)
 */
DEV_INLINE uint2 chi(const uint2 a, const uint2 b, const uint2 c)
{
#if __CUDA_ARCH__ >= 500 && CUDA_VERSION >= 7050
    // Hardware-accelerated implementation using LOP3 instruction
    // 0xD2 corresponds to the operation a ^ ((~b) & c)
    uint2 result;
    asm volatile(
        "// chi\n\t"
        "lop3.b32 %0, %2, %3, %4, 0xD2;\n\t"  // result.x = a.x ^ ((~b.x) & c.x)
        "lop3.b32 %1, %5, %6, %7, 0xD2;"      // result.y = a.y ^ ((~b.y) & c.y)
        : "=r"(result.x), "=r"(result.y)
        : "r"(a.x), "r"(b.x), "r"(c.x), "r"(a.y), "r"(b.y), "r"(c.y));
    return result;
#else
    // Fallback implementation for older architectures
    return a ^ (~b) & c;
#endif
}

/**
 * @brief Initializes the state for Keccak-f1600 permutation
 * This function sets up the state with the header and performs initial rounds
 *
 * @param state Array to hold the state, must be at least 12 uint2 elements
 */
DEV_INLINE void keccak_f1600_init(uint2* state)
{
    // Initialize state array and temporary variables
    uint2 s[25];
    uint2 t[5], u, v;
    const uint2 u2zero = make_uint2(0, 0);

    // Load header data into state and apply initial values
    devectorize2(d_header.uint4s[0], s[0], s[1]);
    devectorize2(d_header.uint4s[1], s[2], s[3]);
    s[4] = state[4];
    s[5] = make_uint2(1, 0);
    s[6] = u2zero;
    s[7] = u2zero;
    s[8] = make_uint2(0, 0x80000000);

    // Initialize remaining state elements to zero
    for (uint32_t i = 9; i < 25; i++)
    {
        s[i] = u2zero;
    }

    // Theta step - part 1: compute column parities
    t[0].x = s[0].x ^ s[5].x;
    t[0].y = s[0].y;
    t[1] = s[1];
    t[2] = s[2];
    t[3].x = s[3].x;
    t[3].y = s[3].y ^ s[8].y;
    t[4] = s[4];

    // Theta step - part 2: apply transformation to each column
    u = t[4] ^ ROL2(t[1], 1);
    s[0] ^= u;
    s[5] ^= u;
    s[10] ^= u;
    s[15] ^= u;
    s[20] ^= u;

    u = t[0] ^ ROL2(t[2], 1);
    s[1] ^= u;
    s[6] ^= u;
    s[11] ^= u;
    s[16] ^= u;
    s[21] ^= u;

    u = t[1] ^ ROL2(t[3], 1);
    s[2] ^= u;
    s[7] ^= u;
    s[12] ^= u;
    s[17] ^= u;
    s[22] ^= u;

    u = t[2] ^ ROL2(t[4], 1);
    s[3] ^= u;
    s[8] ^= u;
    s[13] ^= u;
    s[18] ^= u;
    s[23] ^= u;

    u = t[3] ^ ROL2(t[0], 1);
    s[4] ^= u;
    s[9] ^= u;
    s[14] ^= u;
    s[19] ^= u;
    s[24] ^= u;

    // Rho and Pi steps: rotate bits and rearrange positions
    u = s[1];
    s[1] = ROL2(s[6], 44);
    s[6] = ROL2(s[9], 20);
    s[9] = ROL2(s[22], 61);
    s[22] = ROL2(s[14], 39);
    s[14] = ROL2(s[20], 18);
    s[20] = ROL2(s[2], 62);
    s[2] = ROL2(s[12], 43);
    s[12] = ROL2(s[13], 25);
    s[13] = ROL8(s[19]);
    s[19] = ROR8(s[23]);
    s[23] = ROL2(s[15], 41);
    s[15] = ROL2(s[4], 27);
    s[4] = ROL2(s[24], 14);
    s[24] = ROL2(s[21], 2);
    s[21] = ROL2(s[8], 55);
    s[8] = ROL2(s[16], 45);
    s[16] = ROL2(s[5], 36);
    s[5] = ROL2(s[3], 28);
    s[3] = ROL2(s[18], 21);
    s[18] = ROL2(s[17], 15);
    s[17] = ROL2(s[11], 10);
    s[11] = ROL2(s[7], 6);
    s[7] = ROL2(s[10], 3);
    s[10] = ROL2(u, 1);

    // Chi step: apply non-linear transformation to each row
    // First row
    u = s[0];
    v = s[1];
    s[0] = chi(s[0], s[1], s[2]);
    s[1] = chi(s[1], s[2], s[3]);
    s[2] = chi(s[2], s[3], s[4]);
    s[3] = chi(s[3], s[4], u);
    s[4] = chi(s[4], u, v);

    // Second row
    u = s[5];
    v = s[6];
    s[5] = chi(s[5], s[6], s[7]);
    s[6] = chi(s[6], s[7], s[8]);
    s[7] = chi(s[7], s[8], s[9]);
    s[8] = chi(s[8], s[9], u);
    s[9] = chi(s[9], u, v);

    // Third row
    u = s[10];
    v = s[11];
    s[10] = chi(s[10], s[11], s[12]);
    s[11] = chi(s[11], s[12], s[13]);
    s[12] = chi(s[12], s[13], s[14]);
    s[13] = chi(s[13], s[14], u);
    s[14] = chi(s[14], u, v);

    // Fourth row
    u = s[15];
    v = s[16];
    s[15] = chi(s[15], s[16], s[17]);
    s[16] = chi(s[16], s[17], s[18]);
    s[17] = chi(s[17], s[18], s[19]);
    s[18] = chi(s[18], s[19], u);
    s[19] = chi(s[19], u, v);

    // Fifth row
    u = s[20];
    v = s[21];
    s[20] = chi(s[20], s[21], s[22]);
    s[21] = chi(s[21], s[22], s[23]);
    s[22] = chi(s[22], s[23], s[24]);
    s[23] = chi(s[23], s[24], u);
    s[24] = chi(s[24], u, v);

    // Iota step: XOR first element with round constant
    s[0] ^= keccak_round_constants[0];

    // Main loop for remaining rounds
    for (int i = 1; i < 23; i++)
    {
        // Theta step - compute column parities
        t[0] = xor5(s[0], s[5], s[10], s[15], s[20]);
        t[1] = xor5(s[1], s[6], s[11], s[16], s[21]);
        t[2] = xor5(s[2], s[7], s[12], s[17], s[22]);
        t[3] = xor5(s[3], s[8], s[13], s[18], s[23]);
        t[4] = xor5(s[4], s[9], s[14], s[19], s[24]);

        // Theta step - apply transformation to each column
        u = t[4] ^ ROL2(t[1], 1);
        s[0] ^= u;
        s[5] ^= u;
        s[10] ^= u;
        s[15] ^= u;
        s[20] ^= u;

        u = t[0] ^ ROL2(t[2], 1);
        s[1] ^= u;
        s[6] ^= u;
        s[11] ^= u;
        s[16] ^= u;
        s[21] ^= u;

        u = t[1] ^ ROL2(t[3], 1);
        s[2] ^= u;
        s[7] ^= u;
        s[12] ^= u;
        s[17] ^= u;
        s[22] ^= u;

        u = t[2] ^ ROL2(t[4], 1);
        s[3] ^= u;
        s[8] ^= u;
        s[13] ^= u;
        s[18] ^= u;
        s[23] ^= u;

        u = t[3] ^ ROL2(t[0], 1);
        s[4] ^= u;
        s[9] ^= u;
        s[14] ^= u;
        s[19] ^= u;
        s[24] ^= u;

        // Rho and Pi steps: rotate bits and rearrange positions
        u = s[1];
        s[1] = ROL2(s[6], 44);
        s[6] = ROL2(s[9], 20);
        s[9] = ROL2(s[22], 61);
        s[22] = ROL2(s[14], 39);
        s[14] = ROL2(s[20], 18);
        s[20] = ROL2(s[2], 62);
        s[2] = ROL2(s[12], 43);
        s[12] = ROL2(s[13], 25);
        s[13] = ROL8(s[19]);
        s[19] = ROR8(s[23]);
        s[23] = ROL2(s[15], 41);
        s[15] = ROL2(s[4], 27);
        s[4] = ROL2(s[24], 14);
        s[24] = ROL2(s[21], 2);
        s[21] = ROL2(s[8], 55);
        s[8] = ROL2(s[16], 45);
        s[16] = ROL2(s[5], 36);
        s[5] = ROL2(s[3], 28);
        s[3] = ROL2(s[18], 21);
        s[18] = ROL2(s[17], 15);
        s[17] = ROL2(s[11], 10);
        s[11] = ROL2(s[7], 6);
        s[7] = ROL2(s[10], 3);
        s[10] = ROL2(u, 1);

        // Chi step: apply non-linear transformation to each row
        // First row
        u = s[0];
        v = s[1];
        s[0] = chi(s[0], s[1], s[2]);
        s[1] = chi(s[1], s[2], s[3]);
        s[2] = chi(s[2], s[3], s[4]);
        s[3] = chi(s[3], s[4], u);
        s[4] = chi(s[4], u, v);

        // Second row
        u = s[5];
        v = s[6];
        s[5] = chi(s[5], s[6], s[7]);
        s[6] = chi(s[6], s[7], s[8]);
        s[7] = chi(s[7], s[8], s[9]);
        s[8] = chi(s[8], s[9], u);
        s[9] = chi(s[9], u, v);

        // Third row
        u = s[10];
        v = s[11];
        s[10] = chi(s[10], s[11], s[12]);
        s[11] = chi(s[11], s[12], s[13]);
        s[12] = chi(s[12], s[13], s[14]);
        s[13] = chi(s[13], s[14], u);
        s[14] = chi(s[14], u, v);

        // Fourth row
        u = s[15];
        v = s[16];
        s[15] = chi(s[15], s[16], s[17]);
        s[16] = chi(s[16], s[17], s[18]);
        s[17] = chi(s[17], s[18], s[19]);
        s[18] = chi(s[18], s[19], u);
        s[19] = chi(s[19], u, v);

        // Fifth row
        u = s[20];
        v = s[21];
        s[20] = chi(s[20], s[21], s[22]);
        s[21] = chi(s[21], s[22], s[23]);
        s[22] = chi(s[22], s[23], s[24]);
        s[23] = chi(s[23], s[24], u);
        s[24] = chi(s[24], u, v);

        // Iota step: XOR first element with round constant
        s[0] ^= keccak_round_constants[i];
    }

    // Final theta step - compute column parities
    t[0] = xor5(s[0], s[5], s[10], s[15], s[20]);
    t[1] = xor5(s[1], s[6], s[11], s[16], s[21]);
    t[2] = xor5(s[2], s[7], s[12], s[17], s[22]);
    t[3] = xor5(s[3], s[8], s[13], s[18], s[23]);
    t[4] = xor5(s[4], s[9], s[14], s[19], s[24]);

    // Final theta step - partial application to selected columns
    u = t[4] ^ ROL2(t[1], 1);
    s[0] ^= u;
    s[10] ^= u;

    u = t[0] ^ ROL2(t[2], 1);
    s[6] ^= u;
    s[16] ^= u;

    u = t[1] ^ ROL2(t[3], 1);
    s[12] ^= u;
    s[22] ^= u;

    u = t[2] ^ ROL2(t[4], 1);
    s[3] ^= u;
    s[18] ^= u;

    u = t[3] ^ ROL2(t[0], 1);
    s[9] ^= u;
    s[24] ^= u;

    // Final rho and pi steps for specific elements
    u = s[1];
    s[1] = ROL2(s[6], 44);
    s[6] = ROL2(s[9], 20);
    s[9] = ROL2(s[22], 61);
    s[2] = ROL2(s[12], 43);
    s[4] = ROL2(s[24], 14);
    s[8] = ROL2(s[16], 45);
    s[5] = ROL2(s[3], 28);
    s[3] = ROL2(s[18], 21);
    s[7] = ROL2(s[10], 3);

    // Final chi step for specific elements
    u = s[0];
    v = s[1];
    s[0] = chi(s[0], s[1], s[2]);
    s[1] = chi(s[1], s[2], s[3]);
    s[2] = chi(s[2], s[3], s[4]);
    s[3] = chi(s[3], s[4], u);
    s[4] = chi(s[4], u, v);
    s[5] = chi(s[5], s[6], s[7]);
    s[6] = chi(s[6], s[7], s[8]);
    s[7] = chi(s[7], s[8], s[9]);

    // Final iota step
    s[0] ^= keccak_round_constants[23];

    // Copy first 12 elements to output state
    for (int i = 0; i < 12; ++i)
    {
        state[i] = s[i];
    }
}

/**
 * @brief Finalizes Keccak-f1600 permutation and returns a 64-bit result
 *
 * @param state Input state array
 * @return Final 64-bit hash value
 */
DEV_INLINE uint64_t keccak_f1600_final(uint2* state)
{
    uint2 s[25];
    uint2 t[5], u, v;
    const uint2 u2zero = make_uint2(0, 0);

    // Copy input state to working array
    for (int i = 0; i < 12; ++i)
    {
        s[i] = state[i];
    }

    // Set up padding and constants
    s[12] = make_uint2(1, 0);
    s[13] = u2zero;
    s[14] = u2zero;
    s[15] = u2zero;
    s[16] = make_uint2(0, 0x80000000);

    // Initialize remaining elements to zero
    for (uint32_t i = 17; i < 25; i++)
    {
        s[i] = u2zero;
    }

    // Theta step - compute column parities (optimized for sparse state)
    t[0] = xor3(s[0], s[5], s[10]);
    t[1] = xor3(s[1], s[6], s[11]) ^ s[16];
    t[2] = xor3(s[2], s[7], s[12]);
    t[3] = s[3] ^ s[8];
    t[4] = s[4] ^ s[9];

    // Theta step - apply transformation to each column
    u = t[4] ^ ROL2(t[1], 1);
    s[0] ^= u;
    s[5] ^= u;
    s[10] ^= u;
    s[15] ^= u;
    s[20] ^= u;

    u = t[0] ^ ROL2(t[2], 1);
    s[1] ^= u;
    s[6] ^= u;
    s[11] ^= u;
    s[16] ^= u;
    s[21] ^= u;

    u = t[1] ^ ROL2(t[3], 1);
    s[2] ^= u;
    s[7] ^= u;
    s[12] ^= u;
    s[17] ^= u;
    s[22] ^= u;

    u = t[2] ^ ROL2(t[4], 1);
    s[3] ^= u;
    s[8] ^= u;
    s[13] ^= u;
    s[18] ^= u;
    s[23] ^= u;

    u = t[3] ^ ROL2(t[0], 1);
    s[4] ^= u;
    s[9] ^= u;
    s[14] ^= u;
    s[19] ^= u;
    s[24] ^= u;

    // Rho and Pi steps: rotate bits and rearrange positions
    u = s[1];
    s[1] = ROL2(s[6], 44);
    s[6] = ROL2(s[9], 20);
    s[9] = ROL2(s[22], 61);
    s[22] = ROL2(s[14], 39);
    s[14] = ROL2(s[20], 18);
    s[20] = ROL2(s[2], 62);
    s[2] = ROL2(s[12], 43);
    s[12] = ROL2(s[13], 25);
    s[13] = ROL8(s[19]);
    s[19] = ROR8(s[23]);
    s[23] = ROL2(s[15], 41);
    s[15] = ROL2(s[4], 27);
    s[4] = ROL2(s[24], 14);
    s[24] = ROL2(s[21], 2);
    s[21] = ROL2(s[8], 55);
    s[8] = ROL2(s[16], 45);
    s[16] = ROL2(s[5], 36);
    s[5] = ROL2(s[3], 28);
    s[3] = ROL2(s[18], 21);
    s[18] = ROL2(s[17], 15);
    s[17] = ROL2(s[11], 10);
    s[11] = ROL2(s[7], 6);
    s[7] = ROL2(s[10], 3);
    s[10] = ROL2(u, 1);

        // Chi step: apply non-linear transformation to each row
    // First row
    u = s[0];
    v = s[1];
    s[0] = chi(s[0], s[1], s[2]);
    s[1] = chi(s[1], s[2], s[3]);
    s[2] = chi(s[2], s[3], s[4]);
    s[3] = chi(s[3], s[4], u);
    s[4] = chi(s[4], u, v);

    // Second row
    u = s[5];
    v = s[6];
    s[5] = chi(s[5], s[6], s[7]);
    s[6] = chi(s[6], s[7], s[8]);
    s[7] = chi(s[7], s[8], s[9]);
    s[8] = chi(s[8], s[9], u);
    s[9] = chi(s[9], u, v);

    // Third row
    u = s[10];
    v = s[11];
    s[10] = chi(s[10], s[11], s[12]);
    s[11] = chi(s[11], s[12], s[13]);
    s[12] = chi(s[12], s[13], s[14]);
    s[13] = chi(s[13], s[14], u);
    s[14] = chi(s[14], u, v);

    // Fourth row
    u = s[15];
    v = s[16];
    s[15] = chi(s[15], s[16], s[17]);
    s[16] = chi(s[16], s[17], s[18]);
    s[17] = chi(s[17], s[18], s[19]);
    s[18] = chi(s[18], s[19], u);
    s[19] = chi(s[19], u, v);

    // Fifth row
    u = s[20];
    v = s[21];
    s[20] = chi(s[20], s[21], s[22]);
    s[21] = chi(s[21], s[22], s[23]);
    s[22] = chi(s[22], s[23], s[24]);
    s[23] = chi(s[23], s[24], u);
    s[24] = chi(s[24], u, v);

    // Iota step: XOR first element with round constant
    s[0] ^= keccak_round_constants[0];

    // Main loop for keccak-f1600 permutation rounds
    for (int i = 1; i < 23; i++)
    {
        // Theta step - compute column parities
        t[0] = xor5(s[0], s[5], s[10], s[15], s[20]);
        t[1] = xor5(s[1], s[6], s[11], s[16], s[21]);
        t[2] = xor5(s[2], s[7], s[12], s[17], s[22]);
        t[3] = xor5(s[3], s[8], s[13], s[18], s[23]);
        t[4] = xor5(s[4], s[9], s[14], s[19], s[24]);

        // Theta step - apply transformation to each column
        u = t[4] ^ ROL2(t[1], 1);
        s[0] ^= u;
        s[5] ^= u;
        s[10] ^= u;
        s[15] ^= u;
        s[20] ^= u;

        u = t[0] ^ ROL2(t[2], 1);
        s[1] ^= u;
        s[6] ^= u;
        s[11] ^= u;
        s[16] ^= u;
        s[21] ^= u;

        u = t[1] ^ ROL2(t[3], 1);
        s[2] ^= u;
        s[7] ^= u;
        s[12] ^= u;
        s[17] ^= u;
        s[22] ^= u;

        u = t[2] ^ ROL2(t[4], 1);
        s[3] ^= u;
        s[8] ^= u;
        s[13] ^= u;
        s[18] ^= u;
        s[23] ^= u;

        u = t[3] ^ ROL2(t[0], 1);
        s[4] ^= u;
        s[9] ^= u;
        s[14] ^= u;
        s[19] ^= u;
        s[24] ^= u;

        // Rho and Pi steps: rotate bits and rearrange positions
        u = s[1];
        s[1] = ROL2(s[6], 44);
        s[6] = ROL2(s[9], 20);
        s[9] = ROL2(s[22], 61);
        s[22] = ROL2(s[14], 39);
        s[14] = ROL2(s[20], 18);
        s[20] = ROL2(s[2], 62);
        s[2] = ROL2(s[12], 43);
        s[12] = ROL2(s[13], 25);
        s[13] = ROL8(s[19]);
        s[19] = ROR8(s[23]);
        s[23] = ROL2(s[15], 41);
        s[15] = ROL2(s[4], 27);
        s[4] = ROL2(s[24], 14);
        s[24] = ROL2(s[21], 2);
        s[21] = ROL2(s[8], 55);
        s[8] = ROL2(s[16], 45);
        s[16] = ROL2(s[5], 36);
        s[5] = ROL2(s[3], 28);
        s[3] = ROL2(s[18], 21);
        s[18] = ROL2(s[17], 15);
        s[17] = ROL2(s[11], 10);
        s[11] = ROL2(s[7], 6);
        s[7] = ROL2(s[10], 3);
        s[10] = ROL2(u, 1);

        // Chi step: apply non-linear transformation to each row
        // First row
        u = s[0];
        v = s[1];
        s[0] = chi(s[0], s[1], s[2]);
        s[1] = chi(s[1], s[2], s[3]);
        s[2] = chi(s[2], s[3], s[4]);
        s[3] = chi(s[3], s[4], u);
        s[4] = chi(s[4], u, v);

        // Second row
        u = s[5];
        v = s[6];
        s[5] = chi(s[5], s[6], s[7]);
        s[6] = chi(s[6], s[7], s[8]);
        s[7] = chi(s[7], s[8], s[9]);
        s[8] = chi(s[8], s[9], u);
        s[9] = chi(s[9], u, v);

        // Third row
        u = s[10];
        v = s[11];
        s[10] = chi(s[10], s[11], s[12]);
        s[11] = chi(s[11], s[12], s[13]);
        s[12] = chi(s[12], s[13], s[14]);
        s[13] = chi(s[13], s[14], u);
        s[14] = chi(s[14], u, v);

        // Fourth row
        u = s[15];
        v = s[16];
        s[15] = chi(s[15], s[16], s[17]);
        s[16] = chi(s[16], s[17], s[18]);
        s[17] = chi(s[17], s[18], s[19]);
        s[18] = chi(s[18], s[19], u);
        s[19] = chi(s[19], u, v);

        // Fifth row
        u = s[20];
        v = s[21];
        s[20] = chi(s[20], s[21], s[22]);
        s[21] = chi(s[21], s[22], s[23]);
        s[22] = chi(s[22], s[23], s[24]);
        s[23] = chi(s[23], s[24], u);
        s[24] = chi(s[24], u, v);

        // Iota step: XOR first element with round constant
        s[0] ^= keccak_round_constants[i];
    }

    // Final computation for the output
    t[0] = xor5(s[0], s[5], s[10], s[15], s[20]);
    t[1] = xor5(s[1], s[6], s[11], s[16], s[21]);
    t[2] = xor5(s[2], s[7], s[12], s[17], s[22]);
    t[3] = xor5(s[3], s[8], s[13], s[18], s[23]);
    t[4] = xor5(s[4], s[9], s[14], s[19], s[24]);

    // Apply final transformations to specific state elements
    s[0] = xor3(s[0], t[4], ROL2(t[1], 1));
    s[6] = xor3(s[6], t[0], ROL2(t[2], 1));
    s[12] = xor3(s[12], t[1], ROL2(t[3], 1));

    // Final rotations
    s[1] = ROL2(s[6], 44);
    s[2] = ROL2(s[12], 43);

    // Final chi operation and XOR with round constant
    s[0] = chi(s[0], s[1], s[2]);

    // Return final 64-bit result
    return devectorize(s[0] ^ keccak_round_constants[23]);
}

/**
 * @brief Implements SHA3-512 hashing algorithm
 *
 * @param s State array, must have at least 25 uint2 elements
 */
DEV_INLINE void SHA3_512(uint2* s)
{
    uint2 t[5], u, v;

    // Initialize higher indices of state to zero and set padding
    for (uint32_t i = 8; i < 25; i++)
    {
        s[i] = make_uint2(0, 0);
    }
    s[8].x = 1;           // Set first byte of padding
    s[8].y = 0x80000000;  // Set last byte of padding

    // Perform 23 rounds of the keccak-f permutation
    for (int i = 0; i < 23; i++)
    {
        // Theta step - compute column parities
        t[0] = xor5(s[0], s[5], s[10], s[15], s[20]);
        t[1] = xor5(s[1], s[6], s[11], s[16], s[21]);
        t[2] = xor5(s[2], s[7], s[12], s[17], s[22]);
        t[3] = xor5(s[3], s[8], s[13], s[18], s[23]);
        t[4] = xor5(s[4], s[9], s[14], s[19], s[24]);

        // Theta step - apply transformation to each column
        u = t[4] ^ ROL2(t[1], 1);
        s[0] ^= u;
        s[5] ^= u;
        s[10] ^= u;
        s[15] ^= u;
        s[20] ^= u;

        u = t[0] ^ ROL2(t[2], 1);
        s[1] ^= u;
        s[6] ^= u;
        s[11] ^= u;
        s[16] ^= u;
        s[21] ^= u;

        u = t[1] ^ ROL2(t[3], 1);
        s[2] ^= u;
        s[7] ^= u;
        s[12] ^= u;
        s[17] ^= u;
        s[22] ^= u;

        u = t[2] ^ ROL2(t[4], 1);
        s[3] ^= u;
        s[8] ^= u;
        s[13] ^= u;
        s[18] ^= u;
        s[23] ^= u;

        u = t[3] ^ ROL2(t[0], 1);
        s[4] ^= u;
        s[9] ^= u;
        s[14] ^= u;
        s[19] ^= u;
        s[24] ^= u;

        // Rho and Pi steps: rotate bits and rearrange positions
        u = s[1];
        s[1] = ROL2(s[6], 44);
        s[6] = ROL2(s[9], 20);
        s[9] = ROL2(s[22], 61);
        s[22] = ROL2(s[14], 39);
        s[14] = ROL2(s[20], 18);
        s[20] = ROL2(s[2], 62);
        s[2] = ROL2(s[12], 43);
        s[12] = ROL2(s[13], 25);
        s[13] = ROL2(s[19], 8);
        s[19] = ROL2(s[23], 56);
        s[23] = ROL2(s[15], 41);
        s[15] = ROL2(s[4], 27);
        s[4] = ROL2(s[24], 14);
        s[24] = ROL2(s[21], 2);
        s[21] = ROL2(s[8], 55);
        s[8] = ROL2(s[16], 45);
        s[16] = ROL2(s[5], 36);
        s[5] = ROL2(s[3], 28);
        s[3] = ROL2(s[18], 21);
        s[18] = ROL2(s[17], 15);
        s[17] = ROL2(s[11], 10);
        s[11] = ROL2(s[7], 6);
        s[7] = ROL2(s[10], 3);
        s[10] = ROL2(u, 1);

        // Chi step: apply non-linear transformation to each row
        // First row
        u = s[0];
        v = s[1];
        s[0] = chi(s[0], s[1], s[2]);
        s[1] = chi(s[1], s[2], s[3]);
        s[2] = chi(s[2], s[3], s[4]);
        s[3] = chi(s[3], s[4], u);
        s[4] = chi(s[4], u, v);

        // Second row
        u = s[5];
        v = s[6];
        s[5] = chi(s[5], s[6], s[7]);
        s[6] = chi(s[6], s[7], s[8]);
        s[7] = chi(s[7], s[8], s[9]);
        s[8] = chi(s[8], s[9], u);
        s[9] = chi(s[9], u, v);

        // Third row
        u = s[10];
        v = s[11];
        s[10] = chi(s[10], s[11], s[12]);
        s[11] = chi(s[11], s[12], s[13]);
        s[12] = chi(s[12], s[13], s[14]);
        s[13] = chi(s[13], s[14], u);
        s[14] = chi(s[14], u, v);

        // Fourth row
        u = s[15];
        v = s[16];
        s[15] = chi(s[15], s[16], s[17]);
        s[16] = chi(s[16], s[17], s[18]);
        s[17] = chi(s[17], s[18], s[19]);
        s[18] = chi(s[18], s[19], u);
        s[19] = chi(s[19], u, v);

        // Fifth row
        u = s[20];
        v = s[21];
        s[20] = chi(s[20], s[21], s[22]);
        s[21] = chi(s[21], s[22], s[23]);
        s[22] = chi(s[22], s[23], s[24]);
        s[23] = chi(s[23], s[24], u);
        s[24] = chi(s[24], u, v);

        // Iota step: XOR first element with round constant
        s[0] ^= LDG(keccak_round_constants[i]);
    }

    // Final theta step - compute column parities
    t[0] = xor5(s[0], s[5], s[10], s[15], s[20]);
    t[1] = xor5(s[1], s[6], s[11], s[16], s[21]);
    t[2] = xor5(s[2], s[7], s[12], s[17], s[22]);
    t[3] = xor5(s[3], s[8], s[13], s[18], s[23]);
    t[4] = xor5(s[4], s[9], s[14], s[19], s[24]);

    // Final theta step - apply transformation to selected columns
    u = t[4] ^ ROL2(t[1], 1);
    s[0] ^= u;
    s[10] ^= u;

    u = t[0] ^ ROL2(t[2], 1);
    s[6] ^= u;
    s[16] ^= u;

    u = t[1] ^ ROL2(t[3], 1);
    s[12] ^= u;
    s[22] ^= u;

    u = t[2] ^ ROL2(t[4], 1);
    s[3] ^= u;
    s[18] ^= u;

    u = t[3] ^ ROL2(t[0], 1);
    s[9] ^= u;
    s[24] ^= u;

    // Final rho and pi steps for selected elements
    u = s[1];
    s[1] = ROL2(s[6], 44);
    s[6] = ROL2(s[9], 20);
    s[9] = ROL2(s[22], 61);
    s[2] = ROL2(s[12], 43);
    s[4] = ROL2(s[24], 14);
    s[8] = ROL2(s[16], 45);
    s[5] = ROL2(s[3], 28);
    s[3] = ROL2(s[18], 21);
    s[7] = ROL2(s[10], 3);

    // Final chi step for selected elements
    u = s[0];
    v = s[1];
    s[0] = chi(s[0], s[1], s[2]);
    s[1] = chi(s[1], s[2], s[3]);
    s[2] = chi(s[2], s[3], s[4]);
    s[3] = chi(s[3], s[4], u);
    s[4] = chi(s[4], u, v);
    s[5] = chi(s[5], s[6], s[7]);
    s[6] = chi(s[6], s[7], s[8]);
    s[7] = chi(s[7], s[8], s[9]);

    // Final iota step
    s[0] ^= LDG(keccak_round_constants[23]);
}

/**
 * @brief Optimized Keccak-f permutation implementation for CUDA
 *
 * @param st State array, represented as uint64_t array of 25 elements
 */
__device__ __forceinline__ void keccakf_optimized(uint64_t* st)
{
    uint64_t t, bc[5];

// Perform 24 rounds of keccak-f permutation
#pragma unroll
    for (int r = 0; r < 24; r++)
    {
// Theta step: compute column parities
#pragma unroll
        for (int i = 0; i < 5; i++)
        {
            bc[i] = st[i] ^ st[i + 5] ^ st[i + 10] ^ st[i + 15] ^ st[i + 20];
        }

// Theta step: apply transformation to each state element
#pragma unroll
        for (int i = 0; i < 5; i++)
        {
            t = bc[(i + 4) % 5] ^ ROTL64(bc[(i + 1) % 5], 1);

            st[i] ^= t;
            st[i + 5] ^= t;
            st[i + 10] ^= t;
            st[i + 15] ^= t;
            st[i + 20] ^= t;
        }

        // Rho and Pi steps: rotate bits and rearrange positions
        t = st[1];

#pragma unroll
        for (int i = 0; i < 24; i++)
        {
            int j = keccakf_piln[i];
            bc[0] = st[j];
            st[j] = ROTL64(t, keccakf_rotc[i]);
            t = bc[0];
        }

// Chi step: apply non-linear transformation to each row
#pragma unroll
        for (int j = 0; j < 25; j += 5)
        {
#pragma unroll
            for (int i = 0; i < 5; i++)
            {
                bc[i] = st[j + i];
            }

#pragma unroll
            for (int i = 0; i < 5; i++)
            {
                st[j + i] ^= (~bc[(i + 1) % 5]) & bc[(i + 2) % 5];
            }
        }

        // Iota step: XOR first element with round constant
        st[0] ^= keccakf_rndc[r];
    }
}

/**
 * @brief Optimized implementation of Keccak-256 for fixed input size (32 bytes)
 *
 * @param out Output buffer (32 bytes)
 * @param in Input buffer
 * @param inlen Length of input (defaults to 32 bytes)
 */
__device__ void keccak_256_R5(uint8_t* out, const uint8_t* in, size_t inlen = 32)
{
    // Fast path for the common case of 32-byte input
    if (inlen == 32)
    {
        // Zero-initialize state
        uint64_t st[25] = {0};

        // Use vectorized loading for performance
        uint4* in_vec = (uint4*)in;
        uint4* st_vec = (uint4*)st;

        // Copy input data to state (8 bytes at a time)
        st_vec[0] = in_vec[0];
        st_vec[1] = in_vec[1];

        // Apply padding directly to state
        ((uint8_t*)st)[32] ^= 0x01;   // First padding byte
        ((uint8_t*)st)[135] ^= 0x80;  // Last padding byte

        // Apply the permutation function
        keccakf_optimized(st);

        // Copy output with vectorized instructions
        uint4* out_vec = (uint4*)out;
        out_vec[0] = st_vec[0];
        out_vec[1] = st_vec[1];
    }
    else
    {
        // Generic implementation for variable input length
        uint64_t st[25] = {0};
        const size_t rsiz = 136;  // Rate in bytes (1088 bits)

        // Process all input bytes
        for (size_t i = 0; i < inlen; i++)
        {
            ((uint8_t*)st)[i % rsiz] ^= in[i];
            if ((i + 1) % rsiz == 0)
            {
                keccakf_optimized(st);
            }
        }

        // Apply padding
        ((uint8_t*)st)[inlen % rsiz] ^= 0x01;
        ((uint8_t*)st)[rsiz - 1] ^= 0x80;

        // Final permutation
        keccakf_optimized(st);

// Copy first 32 bytes to output
#pragma unroll
        for (int i = 0; i < 32; i++)
        {
            out[i] = ((uint8_t*)st)[i];
        }
    }
}
