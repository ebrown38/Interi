// SPDX-License-Identifier: MIT
#include <interi/audio/VoiceLimiter.h>
#include <interi/core/Assert.h>

namespace interi::audio
{

VoiceLimiter::VoiceLimiter(std::uint32_t capacity)
    : m_slots(capacity)
{
    INTERI_ASSERT(capacity > 0);
}

VoiceAdmission VoiceLimiter::Acquire(float priority)
{
    VoiceAdmission admission;
    std::uint32_t target = Capacity();
    if (m_live < Capacity())
    {
        for (std::uint32_t slot = 0; slot < Capacity(); ++slot)
        {
            if (!m_slots[slot].live)
            {
                target = slot;
                break;
            }
        }
        ++m_live;
    }
    else
    {
        std::uint32_t lowest = 0;
        for (std::uint32_t slot = 1; slot < Capacity(); ++slot)
        {
            const Slot& s = m_slots[slot];
            const Slot& l = m_slots[lowest];
            if (s.priority < l.priority || (s.priority == l.priority && s.order < l.order))
            {
                lowest = slot;
            }
        }
        if (!(priority > m_slots[lowest].priority))
        {
            return admission;
        }
        target = lowest;
        admission.evicted = lowest;
    }
    m_slots[target] = {.priority = priority, .order = m_nextOrder++, .live = true};
    admission.slot = target;
    return admission;
}

void VoiceLimiter::Release(std::uint32_t slot)
{
    INTERI_ASSERT(slot < Capacity() && m_slots[slot].live);
    m_slots[slot].live = false;
    --m_live;
}

void VoiceLimiter::SetPriority(std::uint32_t slot, float priority)
{
    INTERI_ASSERT(slot < Capacity() && m_slots[slot].live);
    m_slots[slot].priority = priority;
}

} // namespace interi::audio
