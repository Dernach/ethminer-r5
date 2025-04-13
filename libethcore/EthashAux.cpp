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

#include "EthashAux.h"

#include <memory>
#include <stdexcept>

namespace dev
{
namespace eth
{

Result EthashAux::eval(int epoch, const h256& _headerHash, uint64_t _nonce) noexcept
{
    try
    {
        // Convert header hash to ethash format
        auto headerHash = ethash::hash256_from_bytes(_headerHash.data());

        // Create epoch context with automatic cleanup using unique_ptr
        std::unique_ptr<ethash_epoch_context, decltype(&ethash_destroy_epoch_context)> context(
            ethash_create_epoch_context(epoch), &ethash_destroy_epoch_context);

        if (!context)
        {
            // Return empty result if context creation failed
            return {h256(), h256()};
        }

        // Perform the ethash hash computation
        ethash_result result = ethash_hash(context.get(), &headerHash, _nonce);

        // Convert the result back to our hash types
        h256 mix{reinterpret_cast<byte*>(result.mix_hash.bytes), h256::ConstructFromPointer};
        h256 final{reinterpret_cast<byte*>(result.final_hash.bytes), h256::ConstructFromPointer};

        return {final, mix};
    }
    catch (...)
    {
        // Ensure we maintain the noexcept guarantee by catching any exceptions
        // Return empty result on error
        return {h256(), h256()};
    }
}

}  // namespace eth
}  // namespace dev
