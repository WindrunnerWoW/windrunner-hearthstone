#include "HsBgWeeklyQuests.h"
#include "WeeklyRotationDraw.h"
#include <cstring>
#include <vector>

namespace Hs
{
    namespace
    {
        enum class Tally
        {
            Unknown, Game, Bought, Sold, HeroPower, Spell, Triple, Buff, Battlecry, Deathrattle, Refresh, Kill, CombatWin,
            Damage, TeamWin, Top4, First, Tier5, Tier6, Tribe
        };

        struct TallyName
        {
            char const* name;
            Tally tally;
        };
        constexpr TallyName kTallies[] = {
            { "game", Tally::Game }, { "bought", Tally::Bought }, { "sold", Tally::Sold },
            { "hero_power", Tally::HeroPower }, { "spell", Tally::Spell }, { "triple", Tally::Triple },
            { "buff", Tally::Buff }, { "battlecry", Tally::Battlecry }, { "deathrattle", Tally::Deathrattle },
            { "refresh", Tally::Refresh }, { "kill", Tally::Kill }, { "combat_win", Tally::CombatWin },
            { "damage", Tally::Damage }, { "team_win", Tally::TeamWin }, { "top4", Tally::Top4 },
            { "first", Tally::First }, { "tier5", Tally::Tier5 }, { "tier6", Tally::Tier6 }, { "tribe", Tally::Tribe },
        };

        Tally ParseTally(char const* name)
        {
            for (TallyName const& entry : kTallies)
                if (std::strcmp(entry.name, name) == 0)
                    return entry.tally;
            return Tally::Unknown;
        }

        // The quests of a tier that may be drawn now. `strict` keeps last week's quest and every family already taken out;
        // the relaxed passes only run when the roster has been trimmed so far that nothing is left.
        std::vector<BgWeeklyQuest const*> Candidates(int tier, uint32_t previous, std::vector<char const*> const& families,
            int relax)
        {
            std::vector<BgWeeklyQuest const*> candidates;
            for (BgWeeklyQuest const& quest : kBgWeeklyQuests)
            {
                if (quest.tier != tier)
                    continue;
                if (relax < 2 && quest.id == previous)
                    continue;
                bool clash = false;
                if (relax < 1)
                    for (char const* family : families)
                        clash = clash || std::strcmp(family, quest.family) == 0;
                if (!clash)
                    candidates.push_back(&quest);
            }
            return candidates;
        }

        BgWeeklyDraw DrawNext(BgWeeklyDraw const& previous, WeeklyRotation::Random& random)
        {
            BgWeeklyDraw draw{};
            std::vector<char const*> families;
            // Hard first, so the rarer families get their pick before the easier tiers narrow the field.
            for (int tier = 2; tier >= 0; --tier)
            {
                std::vector<BgWeeklyQuest const*> candidates;
                for (int relax = 0; relax < 3 && candidates.empty(); ++relax)
                    candidates = Candidates(tier, previous[static_cast<std::size_t>(tier)], families, relax);
                if (candidates.empty())
                    continue;   // the roster has no quest of this tier at all: the slot stays empty
                BgWeeklyQuest const* pick = candidates[random.Below(candidates.size())];
                draw[static_cast<std::size_t>(tier)] = pick->id;
                families.push_back(pick->family);
            }
            return draw;
        }
    }

    BgWeeklyQuest const* FindBgWeeklyQuest(uint32_t id)
    {
        for (BgWeeklyQuest const& quest : kBgWeeklyQuests)
            if (quest.id == id)
                return &quest;
        return nullptr;
    }

    BgWeeklyDraw DrawBgWeeklyQuests(uint64_t week)
    {
        // Not the duel weeklies' seed: the two draws are independent.
        static WeeklyRotation::DrawSequence<3> sequence(0x4247574b4c593231ull, DrawNext);
        return sequence.Get(week);
    }

    bool BgWeeklyTallyKnown(char const* tally)
    {
        return ParseTally(tally) != Tally::Unknown;
    }

    uint32_t BgWeeklyCredit(BgWeeklyQuest const& quest, BgWeeklyGame const& g)
    {
        switch (ParseTally(quest.tally))
        {
            case Tally::Game: return 1;
            case Tally::Bought: return static_cast<uint32_t>(g.bought);
            case Tally::Sold: return static_cast<uint32_t>(g.sold);
            case Tally::HeroPower: return static_cast<uint32_t>(g.heroPowers);
            case Tally::Spell: return static_cast<uint32_t>(g.spells);
            case Tally::Triple: return static_cast<uint32_t>(g.triples);
            case Tally::Buff: return static_cast<uint32_t>(g.buffs);
            case Tally::Battlecry: return static_cast<uint32_t>(g.battlecries);
            case Tally::Deathrattle: return static_cast<uint32_t>(g.deathrattles);
            case Tally::Refresh: return static_cast<uint32_t>(g.refreshes);
            case Tally::Kill: return static_cast<uint32_t>(g.kills);
            case Tally::CombatWin: return static_cast<uint32_t>(g.combatWins);
            case Tally::Damage: return static_cast<uint32_t>(g.damageDealt) / (quest.unit > 0 ? quest.unit : 1);
            case Tally::TeamWin: return g.teamWon ? 1 : 0;
            case Tally::Top4: return g.placement <= 4 ? 1 : 0;
            case Tally::First: return g.placement == 1 ? 1 : 0;
            case Tally::Tier5: return g.tier >= 5 ? 1 : 0;
            case Tally::Tier6: return g.tier >= 6 ? 1 : 0;
            case Tally::Tribe:
                return quest.tribe >= 0 && quest.tribe < 9 ? static_cast<uint32_t>(g.played[quest.tribe]) : 0;
            case Tally::Unknown: break;
        }
        return 0;
    }
}
