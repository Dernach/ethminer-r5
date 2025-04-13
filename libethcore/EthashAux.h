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

#include <chrono>
#include <cstdint>
#include <string>

#include <libdevcore/Common.h>
#include <libdevcore/Exceptions.h>
#include <libdevcore/Worker.h>

#include <ethash/ethash.hpp>
#include <ethash/keccak.hpp>

namespace dev
{
namespace eth
{

/**
 * @brief Represents the result of an Ethash computation
 */
struct Result
{
    h256 value;    // The computed hash result
    h256 mixHash;  // The mix hash component
};

/**
 * @brief Auxiliary class for Ethash algorithm operations
 */
class EthashAux
{
public:
    /**
     * @brief Evaluates the ethash algorithm for a given epoch and header hash
     *
     * @param epoch The epoch number
     * @param _headerHash The header hash to evaluate
     * @param _nonce The nonce value to use in the evaluation
     * @return Result The computed hash and mix hash
     */
    static Result eval(int epoch, const h256& _headerHash, uint64_t _nonce) noexcept;
};

/**
 * @brief Holds context information for a specific mining epoch
 */
struct EpochContext
{
    int epochNumber = 0;                         // Current epoch number
    int lightNumItems = 0;                       // Number of items in the light cache
    size_t lightSize = 0;                        // Size of the light cache in bytes
    const ethash_hash512* lightCache = nullptr;  // Pointer to the light cache data
    int dagNumItems = 0;                         // Number of items in the DAG
    uint64_t dagSize = 0;                        // Size of the DAG in bytes
};

/**
 * @brief Represents a package of work for miners to process
 */
struct WorkPackage
{
    WorkPackage() = default;

    /**
     * @brief Boolean conversion operator
     * @return true if the package contains valid work (header is not empty)
     */
    explicit operator bool() const { return header != h256(); }

    std::string job;  // Job identifier can be anything. Not necessarily a hash

    h256 boundary;  // Target boundary (difficulty)
    h256 header;    // Block header hash. h256() means "pause mining"
    h256 seed;      // Seed hash for DAG generation

    int epoch = -1;  // Epoch number for this work
    int block = -1;  // Block number

    uint64_t startNonce = 0;   // Starting nonce for this work
    uint16_t exSizeBytes = 0;  // Extra data size in bytes

    std::string algo = "ethash";  // Mining algorithm identifier
};

/**
 * @brief Represents a solution to a mining problem
 */
struct Solution
{
    uint64_t nonce;                                // Solution found nonce
    h256 mixHash;                                  // Mix hash component of the solution
    WorkPackage work;                              // WorkPackage this solution refers to
    std::chrono::steady_clock::time_point tstamp;  // Timestamp of found solution
    unsigned midx;                                 // Originating miner Id
};

}  // namespace eth
}  // namespace dev
