#include "../src/quests/HsWeeklyQuests.h"
#include <cstdio>
#include <cstring>
#include <map>

namespace
{
    int checks = 0, failed = 0;
    void Check(bool value, int line)
    {
        ++checks;
        if (!value) { ++failed; std::printf("weekly check failed at %d\n", line); }
    }
}
#define CHECK(value) Check((value), __LINE__)

int main()
{
    Hs::WeeklyDraw previous{};
    unsigned group = 0, eligible = 0;
    std::map<uint32_t, unsigned> seen;
    for (uint64_t week = 0; week < 4000; ++week)
    {
        auto const draw = Hs::DrawWeeklyQuests(week);
        for (int tier = 0; tier < 3; ++tier)
        {
            auto const* quest = Hs::FindWeeklyQuest(draw[tier]);
            CHECK(quest && quest->tier == tier);
            CHECK(draw[tier] != previous[tier]);
            ++seen[draw[tier]];
        }
        for (int a = 0; a < 3; ++a)
            for (int b = a + 1; b < 3; ++b)
                CHECK(std::strcmp(Hs::FindWeeklyQuest(draw[a])->family, Hs::FindWeeklyQuest(draw[b])->family) != 0);
        if (std::strcmp(Hs::FindWeeklyQuest(draw[2])->family, "wins") != 0)
        {
            unsigned const entries = previous[1] && previous[1] < 920110 ? 4 : 5;
            eligible += 1000 / entries;
            if (draw[1] >= 920110) ++group;
        }
        previous = draw;
    }
    CHECK(seen.size() == 23);
    CHECK(group * 1000 > eligible * 85 / 100 && group * 1000 < eligible * 115 / 100);
    CHECK(Hs::DrawWeeklyQuests(2910) == Hs::DrawWeeklyQuests(2910));
    Hs::MatchEnd end;
    end.res = "win"; end.why = "hero"; end.meta.humanClass = "MAGE"; end.ownTurns = 8;
    auto credit = [&](uint32_t id, bool random = false) { return Hs::WeeklyCredit(*Hs::FindWeeklyQuest(id), end, random); };
    CHECK(credit(919980) == 1 && credit(919981) == 1);
    CHECK(credit(920112) == 1 && credit(920118) == 0);
    CHECK(credit(919990) == 0 && credit(919990, true) == 1);
    CHECK(credit(919993) == 1);
    end.ownTurns = 9;
    CHECK(credit(919993) == 0);
    uint32_t streak = 0;
    for (uint32_t win = 1; win <= 3; ++win)
    {
        streak = Hs::NextWeeklyStreak(streak, end);
        CHECK(streak == win);
    }
    end.res = "loss";
    CHECK(Hs::NextWeeklyStreak(2, end) == 0);
    CHECK(Hs::NextWeeklyStreak(3, end) == 3);
    end.res = "draw";
    CHECK(Hs::NextWeeklyStreak(2, end) == 0);
    end.why = "concede"; end.res = "sur";
    CHECK(Hs::NextWeeklyStreak(2, end) == 0);
    // Walking away breaks the streak; only the server's own interruptions leave it standing.
    end.res = "int";
    for (char const* reason : {"logout", "stuck", "teleport", "death"})
    {
        end.why = reason;
        CHECK(Hs::NextWeeklyStreak(2, end) == 0 && !Hs::WeeklyServerInterrupt(end));
    }
    for (char const* reason : {"npc", "gm", "error"})
    {
        end.why = reason;
        CHECK(Hs::NextWeeklyStreak(2, end) == 2 && Hs::WeeklyServerInterrupt(end));
    }
    end.why = "hero";
    end.res = "loss";
    CHECK(credit(919980) == 0 && credit(919981) == 1);
    end.res = "draw"; end.why = "turns";
    CHECK(credit(919981) == 1 && credit(919980) == 0);
    for (char const* reason : {"concede", "logout", "stuck", "teleport", "death", "npc", "gm", "error"})
    {
        end.res = "win"; end.why = reason;
        CHECK(credit(919981) == 0 && credit(919980) == 0);
    }
    // Damage counts in tens; the part below ten carries to the next match, and only a normal end moves it.
    Hs::WeeklyQuest const& damage = *Hs::FindWeeklyQuest(919992);
    end.res = "loss"; end.why = "hero"; end.tallies[0].heroDamage = 29;
    uint32_t carry = 0;
    CHECK(Hs::WeeklyCredit(damage, end, false, &carry) == 2 && carry == 9);
    CHECK(Hs::WeeklyCredit(damage, end, false, &carry) == 3 && carry == 8);
    end.tallies[0].heroDamage = 2;
    CHECK(Hs::WeeklyCredit(damage, end, false, &carry) == 1 && carry == 0);
    end.tallies[0].heroDamage = 7;
    CHECK(Hs::WeeklyCredit(damage, end, false, &carry) == 0 && carry == 7);
    end.why = "concede"; end.tallies[0].heroDamage = 50;
    CHECK(Hs::WeeklyCredit(damage, end, false, &carry) == 0 && carry == 7);
    end.why = "hero";
    CHECK(Hs::WeeklyCredit(damage, end, false) == 5);
    uint32_t other = 4;
    CHECK(Hs::WeeklyCredit(*Hs::FindWeeklyQuest(919982), end, false, &other) == 0 && other == 4);
    std::printf("Weekly quests: %d checks, %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
