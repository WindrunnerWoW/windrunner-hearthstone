#ifndef MOD_HEARTHSTONE_HS_WEEKLY_QUESTS_H
#define MOD_HEARTHSTONE_HS_WEEKLY_QUESTS_H

#include "../match/HsMatchService.h"
#include <array>
#include <cstdint>
#include <iterator>

namespace Hs
{
    struct WeeklyQuest
    {
        uint32_t id;
        uint32_t credit;
        int tier;
        char const* family;
        uint32_t count;
        char const* tally;
        char const* deckClass;
        uint32_t unit;
    };
    inline constexpr WeeklyQuest kWeeklyQuests[] = {
#include "HsWeeklyRows.inc"
    };
    // hearthstone_weekly_match.quest_mask stores bit i for kWeeklyQuests[i] of a paused duel, so the roster order is
    // stored data: the generator keeps it append-only.
    static_assert(std::size(kWeeklyQuests) <= 32, "quest_mask holds one bit per roster entry");
    using WeeklyDraw = std::array<uint32_t, 3>; // Easy, Medium, Hard
    WeeklyDraw DrawWeeklyQuests(uint64_t week);
    WeeklyQuest const* FindWeeklyQuest(uint32_t id);
    bool WeeklyNormalEnd(MatchEnd const& end);
    bool WeeklyServerInterrupt(MatchEnd const& end);
    // Objective units of one match. For a quest counted in units of several (damage in tens), carry is the part
    // below one unit left by earlier matches, updated in place; without it the remainder is dropped.
    uint32_t WeeklyCredit(WeeklyQuest const& quest, MatchEnd const& end, bool randomHard, uint32_t* carry = nullptr);
    uint32_t NextWeeklyStreak(uint32_t streak, MatchEnd const& end);
}
#endif
