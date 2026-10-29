#ifndef MOD_HEARTHSTONE_HS_ALLIANCE_DRUID_RULES_H
#define MOD_HEARTHSTONE_HS_ALLIANCE_DRUID_RULES_H

#include <array>
#include <cstdint>

constexpr uint32_t kHsAllianceDruid = 919517;
constexpr uint32_t kHsAllianceDruidPatience = 919761;
constexpr uint32_t kHsAllianceDruidGrowth = 919762;
constexpr uint32_t kHsAllianceDruidDuel = 919763;
constexpr std::array<uint32_t, 2> kHsAllianceDruidPatienceCredits = { 919518, 919519 };
constexpr std::array<uint32_t, 2> kHsAllianceDruidGrowthCredits = { 919520, 919521 };

// Observe/visit first, report second. Both native counters survive logout and death.
inline uint32_t HsAllianceDruidStep(std::array<uint32_t, 2> const& counters)
{
    for (uint32_t i = 0; i < counters.size(); ++i)
        if (!counters[i])
            return i;
    return 2;
}

struct HsAllianceDruidClock
{
    uint32_t elapsed = 0;
    bool Advance(uint32_t diff)
    {
        constexpr uint32_t duration = 10000;
        elapsed += diff > duration - elapsed ? duration - elapsed : diff;
        return elapsed == duration;
    }
};

// Owner GPS screenshot, 2026-10-04: top of the Cenarion Enclave tower, map 1, instance 0.
constexpr float kHsAllianceDruidViewX = 10184.375977f;
constexpr float kHsAllianceDruidViewY = 2582.193848f;
constexpr float kHsAllianceDruidViewZ = 1367.098633f;
inline bool HsAllianceDruidAtView(uint32_t map, uint32_t instance, float x, float y, float z)
{
    float const dx = x - kHsAllianceDruidViewX;
    float const dy = y - kHsAllianceDruidViewY;
    float const dz = z - kHsAllianceDruidViewZ;
    return map == 1 && instance == 0 && dx * dx + dy * dy + dz * dz <= 25.0f;
}

#endif
