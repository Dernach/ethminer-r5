#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace dev
{
namespace eth
{

// Ethash constants
constexpr size_t EPOCH_LENGTH = 30000;
constexpr size_t HASH_BYTES = 64;
constexpr size_t MIX_BYTES = 128;
constexpr size_t CACHE_ROUNDS = 3;
constexpr size_t DATASET_INIT_SIZE = 1 << 30;  // 1GB at genesis
constexpr size_t DATASET_GROWTH_SIZE = 1 << 23;
constexpr size_t CACHE_INIT_SIZE = 1 << 24;    // 16 MiB originally
constexpr size_t CACHE_GROWTH_SIZE = 1 << 17;  // 128 KB growth per epoch

/**
 * @brief Calculates the cache size for a given epoch
 * @param block Block number
 * @return Cache size in bytes
 */
size_t calcCacheSize(uint64_t block);

/**
 * @brief Calculates the full dataset (DAG) size for a given epoch
 * @param block_number Block number
 * @return Dataset size in bytes
 */
size_t datasetSize(uint64_t block_number);

/**
 * @brief Generates the seed from the block number
 * @param block Block number
 * @param seed 32-byte buffer that will contain the seed
 */
void generateSeed(uint64_t block, uint8_t* seed);

/**
 * @brief Generates the cache content
 * @param dest Cache destination buffer
 * @param cacheSize Cache size in bytes
 * @param seed 32-byte seed
 */
void generateCacheInternal(uint32_t* dest, size_t cacheSize, const uint8_t seed[32]);

/**
 * @brief Wrapper function that coordinates the complete cache generation for an epoch
 * @param dest Cache destination
 * @param epoch Epoch number
 */
void generateCacheFromEpoch(uint32_t* dest, uint64_t epoch);

/**
 * @brief Checks if the system is little-endian
 * @return true if the system is little-endian, false otherwise
 */
bool isLittleEndian();

/**
 * @brief Swaps byte order for big-endian compatibility
 * @param buffer Byte buffer to swap
 * @param size Buffer size
 */
void swapByteOrder(uint8_t* buffer, size_t size);

/**
 * @brief Converts a byte array to hexadecimal representation
 * @param data Data to convert
 * @param length Length of the data
 * @return Hexadecimal string
 */
std::string bytesToHex(const uint8_t* data, size_t length);

}  // namespace eth
}  // namespace dev
