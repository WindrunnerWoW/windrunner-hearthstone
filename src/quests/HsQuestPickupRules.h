#ifndef MOD_HEARTHSTONE_HS_QUEST_PICKUP_RULES_H
#define MOD_HEARTHSTONE_HS_QUEST_PICKUP_RULES_H

#include <cstdint>

// Verified local SpellCastTimes.dbc: fixed durations, not spell IDs or milliseconds.
constexpr uint32_t kHsQuestPickupHalfSecondCastIndex = 3; // 500 ms, minimum 500 ms.
constexpr uint32_t kHsQuestPickupTwoSecondCastIndex = 5; // 2000 ms, minimum 2000 ms.

inline bool HsQuestPickupAvailable(bool spawned, bool deleted, bool lootReady)
{
    return spawned && !deleted && lootReady;
}

#endif
