#include "HsWeeklyQuests.h"
#include "WeeklyRotationDraw.h"
#include <cstring>
#include <vector>

namespace Hs
{
    WeeklyQuest const* FindWeeklyQuest(uint32_t id)
    {
        for (WeeklyQuest const& quest : kWeeklyQuests)
            if (quest.id == id)
                return &quest;
        return nullptr;
    }

    namespace
    {
        // A week's draw depends on the one before it (no repeat of last week's quest, one class group entry).
        WeeklyDraw DrawNext(WeeklyDraw const& previous, WeeklyRotation::Random& random)
        {
            WeeklyDraw draw{};
            std::vector<char const*> families;
            for (int tier = 2; tier >= 0; --tier)
            {
                std::vector<WeeklyQuest const*> candidates;
                for (WeeklyQuest const& quest : kWeeklyQuests)
                {
                    if (quest.tier != tier || quest.id == previous[tier])
                        continue;
                    bool clash = false;
                    for (char const* family : families)
                        clash = clash || std::strcmp(family, quest.family) == 0;
                    // One representative gives the nine classes exactly one draw entry.
                    if (!clash && (quest.id < 920110 || quest.id == 920110))
                        candidates.push_back(&quest);
                }
                // Keep the group eligible even when its representative was last week's class.
                if (tier == 1 && previous[1] == 920110)
                {
                    bool wins = false;
                    for (char const* family : families)
                        wins = wins || std::strcmp(family, "wins") == 0;
                    if (!wins)
                        candidates.push_back(FindWeeklyQuest(920110));
                }
                WeeklyQuest const* pick = candidates[random.Below(candidates.size())];
                if (pick->id == 920110)
                {
                    std::vector<uint32_t> classes;
                    for (uint32_t id = 920110; id <= 920118; ++id)
                        if (id != previous[1])
                            classes.push_back(id);
                    pick = FindWeeklyQuest(classes[random.Below(classes.size())]);
                }
                draw[tier] = pick->id;
                families.push_back(pick->family);
            }
            return draw;
        }
    }

    WeeklyDraw DrawWeeklyQuests(uint64_t week)
    {
        // Seeded apart from every other rotation: the two draws are independent.
        static WeeklyRotation::DrawSequence<3> sequence(0x48535745454b4c59ull, DrawNext);
        return sequence.Get(week);
    }

    bool WeeklyNormalEnd(MatchEnd const& end)
    {
        return (end.res == "win" || end.res == "loss" || end.res == "draw") &&
            (end.why == "hero" || end.why == "turns");
    }

    bool WeeklyServerInterrupt(MatchEnd const& end)
    {
        return end.why == "npc" || end.why == "gm" || end.why == "error";
    }

    uint32_t NextWeeklyStreak(uint32_t streak, MatchEnd const& end)
    {
        if (streak >= 3)
            return 3; // Completion stays ready for turn-in.
        if (WeeklyNormalEnd(end) && end.res == "win")
            return streak + 1;
        // Every other end breaks it, walking away included (logout, teleport, death, Stuck). Only the server's own
        // interruptions (the NPC left, a GM, an error) leave it standing.
        return WeeklyServerInterrupt(end) ? streak : 0;
    }

    uint32_t WeeklyCredit(WeeklyQuest const& quest, MatchEnd const& end, bool randomHard, uint32_t* carry)
    {
        if (!WeeklyNormalEnd(end))
            return 0;
        std::string const tally = quest.tally;
        Match::SideTally const& counts = end.tallies[0];
        if (tally == "turn") return counts.turns;
        if (tally == "hero_power") return counts.heroPowers;
        if (tally == "draw") return counts.cardsDrawn;
        if (tally == "minion_destroyed") return counts.minionsDestroyed;
        if (tally == "minion_played") return counts.minionsPlayed;
        if (tally == "spell_played") return counts.spellsPlayed;
        if (tally == "beast_played") return counts.beastsPlayed;
        if (tally == "hero_damage")
        {
            uint32_t const total = counts.heroDamage + (carry ? *carry : 0);
            if (carry)
                *carry = total % quest.unit;
            return total / quest.unit;
        }
        if (tally == "expensive_played") return counts.expensivePlayed;
        if (tally == "match") return 1;
        if (end.res != "win") return 0;
        if (tally == "win") return 1;
        if (tally == "win_class") return end.meta.humanClass == quest.deckClass ? 1 : 0;
        if (tally == "win_hard_random") return randomHard ? 1 : 0;
        if (tally == "win_fast") return end.ownTurns <= 8 ? 1 : 0;
        return 0;
    }
}
