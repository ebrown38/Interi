// SPDX-License-Identifier: MIT
#include <interi/testing/StateHasher.h>

#include <xxhash.h>

#include <new>

namespace interi::testing
{

void StateHasher::StateDeleter::operator()(XXH3_state_s* state) const noexcept
{
    XXH3_freeState(state);
}

StateHasher::StateHasher()
    : m_state(XXH3_createState())
{
    if (!m_state || XXH3_64bits_reset(m_state.get()) == XXH_ERROR)
    {
        throw std::bad_alloc();
    }
}

void StateHasher::AddBytes(std::span<const std::byte> bytes)
{
    // Byte-wise streaming means Add(a); Add(b) == AddRange({a, b}).
    XXH3_64bits_update(m_state.get(), bytes.data(), bytes.size());
}

std::uint64_t StateHasher::Finish() const
{
    return XXH3_64bits_digest(m_state.get());
}

} // namespace interi::testing
