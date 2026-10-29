#ifndef MOD_HEARTHSTONE_HS_HORDE_PRIEST_RULES_H
#define MOD_HEARTHSTONE_HS_HORDE_PRIEST_RULES_H

#include <array>
#include <cstdint>
#include "HsQuestPickupRules.h"

constexpr uint32_t kHsHordePriest = 919539;
constexpr uint32_t kHsHordePriestFetch = 919791;
constexpr uint32_t kHsHordePriestDuel = 919792;
constexpr std::array<uint32_t, 2> kHsHordePriestItems = { 919643, 919644 };
constexpr std::array<uint32_t, 2> kHsHordePriestObjects = { 919056, 919057 };
constexpr uint32_t kHsHordePriestRequiredCount = 4;
constexpr uint32_t kHsHordePriestRespawnSeconds = 30;

struct HsHordePriestPickupPosition { float x, y, z, orientation; };
// Owner screenshots: map 0, instance 0; first four tablets, last four cards.
// Player Z agrees with the interior floor; no height offset is added.
constexpr std::array<HsHordePriestPickupPosition, 8> kHsHordePriestPositions = {{
    {1638.038940f, 218.692139f, 62.594730f, 1.953450f},
    {1671.509888f, 246.437332f, 62.595966f, 2.584910f},
    {1694.471680f, 232.467117f, 62.595623f, 5.648750f},
    {1722.450317f, 233.810394f, 62.596436f, 5.719436f},
    {1633.892212f, 225.364899f, 62.593319f, 3.301979f},
    {1657.781128f, 242.165634f, 62.592339f, 0.317465f},
    {1713.396606f, 240.287735f, 62.597462f, 0.433707f},
    {1709.223389f, 236.158249f, 62.597633f, 3.237577f}
}};
static_assert(kHsHordePriestPositions.size() ==
    kHsHordePriestObjects.size() * kHsHordePriestRequiredCount, "Four locations per item type");

inline bool HsHordePriestPickupAllowed(bool horde, bool questIncomplete, bool rewarded,
    uint32_t level, bool introductionRewarded, uint32_t countIncludingBank)
{
    return horde && questIncomplete && !rewarded && level >= 10 && introductionRewarded &&
        countIncludingBank < kHsHordePriestRequiredCount;
}

inline uint32_t HsHordePriestPickupViewFlags(bool eligible)
{
    return eligible ? 0x01u : 0x04u; // Native ACTIVATE or NO_INTERACT; chest type enables the client effect.
}

#endif
