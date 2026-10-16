// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace interi::audio
{

/// What Acquire did.
struct VoiceAdmission
{
    std::optional<std::uint32_t> slot;    ///< The newcomer's slot; nothing if it was rejected.
    std::optional<std::uint32_t> evicted; ///< A live slot that was taken over (the lowest priority).
};

/// A fixed number of voice slots, ranked by priority. When all slots are live, a newcomer takes over the
/// lowest-priority slot if it ranks strictly higher; otherwise it is rejected (so a flood of equal, quiet sounds does
/// not churn voices). Among equal lowest priorities the oldest is evicted first. No allocation after construction.
class VoiceLimiter
{
public:
    explicit VoiceLimiter(std::uint32_t capacity);

    VoiceAdmission Acquire(float priority);
    /// Frees a live slot (the voice ended).
    void Release(std::uint32_t slot);
    /// Re-ranks a live slot (e.g. its audible gain changed as the listener moved).
    void SetPriority(std::uint32_t slot, float priority);

    [[nodiscard]] bool IsLive(std::uint32_t slot) const noexcept { return m_slots[slot].live; }
    [[nodiscard]] float Priority(std::uint32_t slot) const noexcept { return m_slots[slot].priority; }
    [[nodiscard]] std::uint32_t Capacity() const noexcept { return static_cast<std::uint32_t>(m_slots.size()); }
    [[nodiscard]] std::uint32_t LiveCount() const noexcept { return m_live; }

private:
    struct Slot
    {
        float priority = 0.0f;
        std::uint64_t order = 0; ///< Acquire sequence number (older = smaller).
        bool live = false;
    };

    std::vector<Slot> m_slots;
    std::uint64_t m_nextOrder = 0;
    std::uint32_t m_live = 0;
};

} // namespace interi::audio
