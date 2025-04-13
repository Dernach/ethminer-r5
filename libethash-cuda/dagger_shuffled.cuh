#include "cuda_helper.h"
#include "ethash_cuda_miner_kernel.h"
#include "ethash_cuda_miner_kernel_globals.h"

namespace
{
/**
 * @brief Converts bytes to uint32_t (little-endian)
 * @param bytes Byte array
 * @param offset Starting offset in array
 * @return Converted uint32_t value
 */
DEV_INLINE uint32_t bytes_to_uint32(const uint8_t* __restrict__ bytes, int offset)
{
#ifdef SAFE_MEMORY_ACCESS
    // Safe, aligned access implementation
    return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
#else
    // Direct cast for better performance on architectures that support unaligned access
    return *reinterpret_cast<const uint32_t*>(bytes + offset);
#endif
}

/**
 * @brief Converts uint32_t to bytes (little-endian)
 * @param value Value to convert
 * @param bytes Destination byte array
 * @param offset Starting offset in array
 */
DEV_INLINE void uint32_to_bytes(
    uint32_t value, uint8_t* __restrict__ bytes, int offset)
{
#ifdef SAFE_MEMORY_ACCESS
    // Safe, aligned write implementation
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
    bytes[offset + 2] = static_cast<uint8_t>(value >> 16);
    bytes[offset + 3] = static_cast<uint8_t>(value >> 24);
#else
    // Direct cast for better performance on architectures that support unaligned access
    *reinterpret_cast<uint32_t*>(bytes + offset) = value;
#endif
}
}  // namespace

/**
 * @brief Implementation of the Ethash-R5 algorithm
 * @param digest Input/output digest array (8 uint32_t values)
 */
DEV_INLINE void ethash_R5(uint32_t* digest)
{
    constexpr int DIGEST_SIZE = 8;
    constexpr int BYTES_SIZE = 32;  // 8 uint32_t * 4 bytes

    // 4 rounds of R5
    for (int round = 0; round < 4; round++)
    {
        // Copy digest to extra for this round
        uint32_t extra[DIGEST_SIZE];

#pragma unroll
        for (int i = 0; i < DIGEST_SIZE; i++)
        {
            extra[i] = digest[i];
        }

// 3 iterations of FNV on extra[0] and extra[7]
#pragma unroll
        for (int k = 0; k < 3; k++)
        {
            uint32_t old_extra0 = extra[0];
            extra[0] = fnv(old_extra0, extra[7]);
        }

        // Apply FNV hash for adjacent mixing as in Go
        fnv_hash(extra, extra);

        // Convert to bytes (little-endian)
        uint8_t digest_bytes[BYTES_SIZE];

#pragma unroll
        for (int i = 0; i < DIGEST_SIZE; i++)
        {
            uint32_to_bytes(extra[i], digest_bytes, i * sizeof(uint32_t));
        }

        uint8_t result[BYTES_SIZE];

        // Calculer le hash Keccak de toute façon
        keccak_256_R5(result, digest_bytes, BYTES_SIZE);

        // Décider d'appliquer ou non le résultat du hash
        bool should_apply = ((digest_bytes[0] & 0x01) == 0);

        if (should_apply)
        {
#pragma unroll
            for (int i = 0; i < BYTES_SIZE; i++)
            {
                digest_bytes[i] = result[i];
            }
        }

// Convert back to uint32_t (little-endian)
#pragma unroll
        for (int i = 0; i < DIGEST_SIZE; i++)
        {
            digest[i] = bytes_to_uint32(digest_bytes, i * sizeof(uint32_t));
        }
    }
}

/**
 * @brief Computes the Ethash hash for a given nonce (optimisé avec compatibilité)
 * @param nonce The nonce to compute the hash for
 * @param mix_hash Output buffer for the mix hash
 * @return true if the hash is less than the target (valid solution), false otherwise
 */
DEV_INLINE bool compute_hash(uint64_t nonce, uint2* mix_hash)
{
    constexpr int STATE_SIZE = 12;
    constexpr int MIX_SIZE = 32;       // 128 bytes = 32 uint32_t
    constexpr int DIGEST_SIZE = 8;     // 32 bytes = 8 uint32_t
    constexpr int COMBINED_SIZE = 96;  // 64 bytes (seed) + 32 bytes (digest)
    constexpr int HASH_SIZE = 32;      // 32 bytes for final hash

    // Initialize state with sha3_512(header .. nonce)
    uint2 state[STATE_SIZE];

    // Garder l'initialisation identique à l'original pour éviter les erreurs
    // Note: Nous ne touchons pas à l'initialisation des états - utilisons le même code que
    // l'original
    state[4] = vectorize(nonce);

    // Utiliser l'initialisation keccak exactement comme l'original
    keccak_f1600_init(state);

    // Initialize mix - identique à l'original
    uint32_t mix[MIX_SIZE];

// Copy state to mix - exactement comme l'original
#pragma unroll
    for (int i = 0; i < 8; i++)
    {
        mix[i * 2] = state[i].x;
        mix[i * 2 + 1] = state[i].y;
    }

// Duplicate first 16 words - identique à l'original
#pragma unroll
    for (int i = 0; i < 16; i++)
    {
        mix[i + 16] = mix[i];
    }

    // Initialize sequence dependency - identique à l'original
    uint32_t seq_dep = state[0].x;

    // Main Ethash-R5 loop - Gardons la structure de la boucle identique
    for (uint32_t a = 0; a < 64; a++)
    {
        uint32_t mix_val = mix[a % MIX_SIZE];

        // Update sequence dependency - identique à l'original
        seq_dep = fnv(seq_dep, mix_val);

        // Calculate parent index - identique à l'original
        uint32_t parent = fnv(a ^ seq_dep, mix_val) % d_dag_size;
        uint32_t idx = parent * 2;

        // Temporary array for DAG elements
        uint32_t temp[MIX_SIZE];

// Gardons l'accès DAG identique à l'original
#pragma unroll
        for (int i = 0; i < 16; i++)
        {
            temp[i] = d_dag[idx].words[i];
            temp[i + 16] = d_dag[idx + 1].words[i];
        }

        // Optimisation de la branche conditionnelle sans changer la logique
        uint32_t new_mix0 = fnv(mix[0], temp[0]);
        mix[0] = (mix[0] & 0x1) ? new_mix0 : mix[0];

// Apply FNV to entire mix - identique à l'original
#pragma unroll
        for (int i = 0; i < MIX_SIZE; i++)
        {
            mix[i] = fnv(mix[i], temp[i]);
        }
    }

    // Compress the mix - exactement comme l'original
    uint32_t digest[DIGEST_SIZE];

#pragma unroll
    for (int i = 0; i < DIGEST_SIZE; i++)
    {
        digest[i] = fnv(fnv(fnv(mix[i * 4], mix[i * 4 + 1]), mix[i * 4 + 2]), mix[i * 4 + 3]);
    }

    // Apply the additional 4 rounds of R5
    ethash_R5(digest);

    // Prepare the final hash - exactement comme l'original
    uint8_t combined[COMBINED_SIZE];

// Convert state to bytes - identique à l'original
#pragma unroll
    for (int i = 0; i < 8; i++)
    {
        uint32_to_bytes(state[i].x, combined, i * 8);
        uint32_to_bytes(state[i].y, combined, i * 8 + 4);
    }

// Convert digest to bytes - identique à l'original
#pragma unroll
    for (int i = 0; i < DIGEST_SIZE; i++)
    {
        uint32_to_bytes(digest[i], combined, 64 + i * 4);
    }

    // Calculate final hash
    uint8_t final_hash[HASH_SIZE];
    keccak_256_R5(final_hash, combined, COMBINED_SIZE);

    // Check against target - identique à l'original
    uint64_t upper64;
    memcpy(&upper64, final_hash, sizeof(uint64_t));
    upper64 = cuda_swab64(upper64);
    bool result = upper64 <= d_target;

    // Update mix_hash for output - identique à l'original
    mix_hash[0].x = digest[0];
    mix_hash[0].y = digest[1];
    mix_hash[1].x = digest[2];
    mix_hash[1].y = digest[3];
    mix_hash[2].x = digest[4];
    mix_hash[2].y = digest[5];
    mix_hash[3].x = digest[6];
    mix_hash[3].y = digest[7];

    return result;
}