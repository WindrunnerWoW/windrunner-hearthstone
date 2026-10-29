#include "../src/quests/HsBgWeeklyQuests.h"
#include <cstdio>
#include <cstring>
#include <map>

namespace
{
    int checks = 0, failed = 0;
    void Check(bool value, int line)
    {
        ++checks;
        if (!value) { ++failed; std::printf("bg weekly check failed at %d\n", line); }
    }
}
#define CHECK(value) Check((value), __LINE__)

int main()
{
    using namespace Hs;
    // The roster: 33 quests, ids and stubs in order, every tribe has its own medium quest.
    CHECK(std::size(kBgWeeklyQuests) == 33);
    int perTier[3] = {};
    bool tribe[9] = {};
    for (BgWeeklyQuest const& q : kBgWeeklyQuests)
    {
        ++perTier[q.tier];
        CHECK(q.count > 0 && q.count <= 63 && q.credit >= 920260 && q.credit <= 920292 && q.unit >= 1);
        CHECK(FindBgWeeklyQuest(q.id) == &q);
        CHECK(BgWeeklyTallyKnown(q.tally));
        if (std::strcmp(q.tally, "tribe") == 0)
        {
            CHECK(q.tier == 1 && q.tribe >= 0 && q.tribe < 9);
            tribe[q.tribe] = true;
        }
        else
            CHECK(q.tribe == -1);
    }
    CHECK(perTier[0] == 7 && perTier[1] == 19 && perTier[2] == 7);
    for (bool t : tribe)
        CHECK(t);
    CHECK(FindBgWeeklyQuest(1) == nullptr && FindBgWeeklyQuest(920209) == nullptr);

    // The draw: one of each tier, no family twice, never last week's quest again, every quest comes up.
    BgWeeklyDraw previous{};
    std::map<uint32_t, unsigned> seen;
    for (uint64_t week = 0; week < 3000; ++week)
    {
        BgWeeklyDraw const draw = DrawBgWeeklyQuests(week);
        CHECK(draw == DrawBgWeeklyQuests(week));   // deterministic
        for (int tier = 0; tier < 3; ++tier)
        {
            BgWeeklyQuest const* q = FindBgWeeklyQuest(draw[static_cast<std::size_t>(tier)]);
            CHECK(q && q->tier == tier);
            CHECK(draw[static_cast<std::size_t>(tier)] != previous[static_cast<std::size_t>(tier)]);
            ++seen[draw[static_cast<std::size_t>(tier)]];
        }
        for (int a = 0; a < 3; ++a)
            for (int b = a + 1; b < 3; ++b)
                CHECK(std::strcmp(FindBgWeeklyQuest(draw[static_cast<std::size_t>(a)])->family,
                    FindBgWeeklyQuest(draw[static_cast<std::size_t>(b)])->family) != 0);
        previous = draw;
    }
    CHECK(seen.size() == 33);

    // The credit of one finished game.
    BgWeeklyGame g;
    g.placement = 1; g.teamWon = true; g.tier = 6; g.heroPowers = 12; g.bought = 31; g.sold = 9; g.spells = 4; g.triples = 3;
    g.played[0] = 7; g.played[8] = 41;
    g.buffs = 22; g.battlecries = 6; g.refreshes = 13; g.deathrattles = 17; g.kills = 29; g.combatWins = 9; g.damageDealt = 187;
    auto credit = [&](char const* tally, int tribeIndex = -1) {
        BgWeeklyQuest q{ 1, 1, 0, "x", 1, tally, tribeIndex };
        return BgWeeklyCredit(q, g);
    };
    CHECK(credit("game") == 1 && credit("bought") == 31 && credit("sold") == 9 && credit("hero_power") == 12);
    CHECK(credit("spell") == 4 && credit("triple") == 3 && credit("team_win") == 1 && credit("first") == 1);
    CHECK(credit("top4") == 1 && credit("tier5") == 1 && credit("tier6") == 1);
    CHECK(credit("tribe", 0) == 7 && credit("tribe", 8) == 41 && credit("tribe", 3) == 0 && credit("tribe", -1) == 0);
    CHECK(credit("nonsense") == 0);
    CHECK(credit("buff") == 22 && credit("battlecry") == 6 && credit("refresh") == 13 && credit("deathrattle") == 17);
    CHECK(credit("kill") == 29 && credit("combat_win") == 9);
    {
        BgWeeklyQuest tens{ 1, 1, 1, "x", 30, "damage", -1, 10 };
        CHECK(BgWeeklyCredit(tens, g) == 18);   // 187 damage = 18 units of ten, the remainder of one game is dropped
    }
    g.placement = 5; g.teamWon = false; g.tier = 5;
    CHECK(credit("team_win") == 0 && credit("first") == 0 && credit("top4") == 0 && credit("tier5") == 1 && credit("tier6") == 0);
    g.placement = 4;
    CHECK(credit("top4") == 1);

    std::printf("bg_weekly_tests: %d checks, %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
