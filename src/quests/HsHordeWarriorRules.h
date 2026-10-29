#ifndef MOD_HEARTHSTONE_HS_HORDE_WARRIOR_RULES_H
#define MOD_HEARTHSTONE_HS_HORDE_WARRIOR_RULES_H

#include <array>
#include <cstdint>

constexpr std::array<uint32_t, 8> kHsHordeSceneTimes = { 0, 8000, 17000, 27000, 37000, 47000, 55000, 60000 };

struct HsHordeSceneClock
{
    uint32_t elapsedMs = 0;
    uint32_t nextLine = 0;

    template<class Speak>
    bool Advance(uint32_t diff, Speak speak)
    {
        elapsedMs += diff > 60000 - elapsedMs ? 60000 - elapsedMs : diff;
        while (nextLine < kHsHordeSceneTimes.size() && elapsedMs >= kHsHordeSceneTimes[nextLine])
            speak(nextLine++);
        return elapsedMs == 60000;
    }
};

inline bool HsHordePickupAllowed(bool horde, bool questIncomplete, bool hasDeck)
{
    return horde && questIncomplete && !hasDeck;
}

// GO_FLAG_NO_INTERACT is per-viewer here, never a global lock on the shared box.
inline uint32_t HsHordeBoxViewFlags(uint32_t baseFlags, bool eligible)
{
    constexpr uint32_t noInteract = 0x10;
    return eligible ? baseFlags & ~noInteract : baseFlags | noInteract;
}

#endif
