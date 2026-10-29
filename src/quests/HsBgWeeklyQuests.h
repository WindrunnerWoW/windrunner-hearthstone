#ifndef MOD_HEARTHSTONE_HS_BG_WEEKLY_QUESTS_H
#define MOD_HEARTHSTONE_HS_BG_WEEKLY_QUESTS_H

#include <array>
#include <cstdint>
#include <iterator>

namespace Hs
{
    // The Warsong Battlegrounds weeklies: their own roster and weekly draw (one Easy, one Medium, one Hard), credited when
    // a game ends. Every quest pays one booster pack. The pure part lives here so it can be tested without the core.
    struct BgWeeklyQuest
    {
        uint32_t id;
        uint32_t credit;       // the credit stub creature of the quest objective
        int tier;              // 0 easy, 1 medium, 2 hard
        char const* family;    // two quests of one family are never drawn together
        uint32_t count;        // in objective units
        char const* tally;     // game bought sold hero_power team_win top4 first tier5 tier6 triple spell tribe buff
                               // battlecry deathrattle refresh kill combat_win damage
        int tribe;             // tribe bit index 0-8 (Beast .. Quilboar) for "tribe", else -1
        uint32_t unit;         // how many of the counted thing make one objective unit (damage in tens); 1 = each
    };
    inline constexpr BgWeeklyQuest kBgWeeklyQuests[] = {
#include "HsBgWeeklyRows.inc"
    };
    using BgWeeklyDraw = std::array<uint32_t, 3>; // Easy, Medium, Hard
    BgWeeklyDraw DrawBgWeeklyQuests(uint64_t week);
    BgWeeklyQuest const* FindBgWeeklyQuest(uint32_t id);

    // What one finished game gave the human (a copy of the engine's GameRecord, so this file stays free of the engine).
    struct BgWeeklyGame
    {
        int placement = 8;         // overall 1 (best) .. 8
        bool teamWon = false;
        int tier = 1;              // the highest tavern tier reached
        int heroPowers = 0;
        int bought = 0;
        int sold = 0;
        int spells = 0;
        int triples = 0;
        int buffs = 0;
        int battlecries = 0;
        int refreshes = 0;
        int deathrattles = 0;
        int kills = 0;
        int combatWins = 0;
        int damageDealt = 0;
        int played[9] = {};        // minions played by tribe bit index
    };
    // The objective units one finished game credits to the quest (0 when it does not count).
    uint32_t BgWeeklyCredit(BgWeeklyQuest const& quest, BgWeeklyGame const& game);
    // Whether the tally name is one the credit knows (a typo in a row would otherwise credit nothing, silently).
    bool BgWeeklyTallyKnown(char const* tally);
}
#endif
