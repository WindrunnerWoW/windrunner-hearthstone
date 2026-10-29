// Standalone tests of the ladder rules (src/ladder/HsLadderRules.*): the star ladder, the hidden rating,
// the ladder order, the roster assignment, the ladder start, the NPC rounds (plan / play / apply) and the player
// result, and the row encodings of the database tables (src/ladder/HsLadderRows.*). Almost everything runs on a fake fast game function; a few tests play real games with the library decks of
// data/sql/world/0002_hearthstone_opponents.sql and the 101 finder names of 0003_hearthstone_finder.sql.
//   ladder_tests.exe [directory]
// Exit code 0 = all checks passed.

#include "../src/ladder/HsLadderDuel.h"
#include "../src/ladder/HsLadderOffers.h"
#include "../src/ladder/HsLadderPlay.h"
#include "../src/ladder/HsLadderRows.h"
#include "../src/ladder/HsLadderRules.h"
#include "../src/ladder/HsLadderWorld.h"
#include "match_all_cards.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Hs;
using namespace Hs::Ladder;

static int g_checks = 0;
static int g_failed = 0;
static char const* g_test = "";

#define CHECK(cond)                                                                                   \
    do {                                                                                              \
        ++g_checks;                                                                                   \
        if (!(cond)) {                                                                                \
            ++g_failed;                                                                               \
            std::printf("FAIL [%s] %s:%d: %s\n", g_test, __FILE__, __LINE__, #cond);                  \
        }                                                                                             \
    } while (0)

#define RUN(fn)                                                                                       \
    do {                                                                                              \
        g_test = #fn;                                                                                 \
        int const before = g_failed;                                                                  \
        fn();                                                                                         \
        std::printf("%-44s %s\n", #fn, g_failed == before ? "ok" : "FAILED");                         \
    } while (0)

namespace
{
    std::string g_dir = ".";

    // ---- the data files ----------------------------------------------------------------------------------

    bool ReadFile(std::string const& path, std::string& out)
    {
        std::ifstream in(path.c_str(), std::ios::binary);
        if (!in)
            return false;
        std::ostringstream ss;
        ss << in.rdbuf();
        out = ss.str();
        return true;
    }

    // The rows of "INSERT INTO `table` (...) VALUES ('a', 'b', ...), (...);" with plain quoted string cells.
    bool ParseStringRows(std::string const& sql, std::string const& table, std::vector<std::vector<std::string>>& rows)
    {
        std::size_t pos = sql.find("INSERT INTO `" + table + "`");
        if (pos == std::string::npos)
            return false;
        pos = sql.find("VALUES", pos);
        if (pos == std::string::npos)
            return false;
        pos += 6;
        std::vector<std::string> row;
        bool inRow = false;
        while (pos < sql.size())
        {
            char const c = sql[pos];
            if (c == '\'')
            {
                std::string value;
                ++pos;
                while (pos < sql.size())
                {
                    if (sql[pos] == '\'' && pos + 1 < sql.size() && sql[pos + 1] == '\'')
                    {
                        value.push_back('\'');
                        pos += 2;
                    }
                    else if (sql[pos] == '\'')
                        break;
                    else
                        value.push_back(sql[pos++]);
                }
                ++pos;
                row.push_back(value);
                continue;
            }
            if (c == '(')
            {
                inRow = true;
                row.clear();
            }
            else if (c == ')' && inRow)
            {
                inRow = false;
                rows.push_back(row);
            }
            else if (c == ';' && !inRow)
                break;
            ++pos;
        }
        return !rows.empty();
    }

    // The library decks of kind "library" (finder normal = easy, hard = hard).
    std::vector<LibraryDeck> const& Library()
    {
        static std::vector<LibraryDeck> decks = []() {
            std::vector<LibraryDeck> out;
            std::string sql;
            if (!ReadFile(g_dir + "/../data/sql/world/0002_hearthstone_opponents.sql", sql))
                return out;
            std::vector<std::vector<std::string>> rows;
            if (!ParseStringRows(sql, "hearthstone_deck_template", rows))
                return out;
            for (std::vector<std::string> const& r : rows)
            {
                if (r.size() != 6 || r[3] != "library")
                    continue;
                LibraryDeck d;
                d.key = r[0];
                d.heroClass = r[2];
                d.rating = (r[4] == "hard" || r[4] == "ladder_hard") ? DeckRating::Hard : DeckRating::Easy;
                std::stringstream cards(r[5]);
                std::string part;
                while (std::getline(cards, part, ','))
                {
                    std::size_t const colon = part.find(':');
                    if (colon == std::string::npos)
                        continue;
                    std::string id = part.substr(0, colon);
                    id.erase(std::remove_if(id.begin(), id.end(), [](unsigned char ch) { return std::isspace(ch) != 0; }), id.end());
                    int const n = std::atoi(part.c_str() + colon + 1);
                    for (int i = 0; i < n; ++i)
                        d.cards.push_back(id);
                }
                out.push_back(d);
            }
            return out;
        }();
        return decks;
    }

    // The enabled rows of hearthstone_finder_npc: "(2496, 'any'), -- Baron Revilgaz".
    std::vector<FinderName> const& Finder()
    {
        static std::vector<FinderName> names = []() {
            std::vector<FinderName> out;
            std::string sql;
            if (!ReadFile(g_dir + "/../data/sql/world/0003_hearthstone_finder.sql", sql))
                return out;
            std::size_t pos = sql.find("INSERT IGNORE INTO `hearthstone_finder_npc`");
            if (pos == std::string::npos)
                return out;
            std::istringstream in(sql.substr(pos));
            std::string line;
            while (std::getline(in, line))
            {
                // "(2496, 'any')": digits after the bracket, the faction between the quotes.
                if (line.empty() || line[0] != '(' || line.size() < 2 || !std::isdigit(static_cast<unsigned char>(line[1])))
                    continue;
                std::size_t const q1 = line.find('\'');
                std::size_t const q2 = q1 == std::string::npos ? q1 : line.find('\'', q1 + 1);
                if (q2 == std::string::npos)
                    continue;
                FinderName n;
                n.entry = static_cast<uint32_t>(std::strtoul(line.c_str() + 1, nullptr, 10));
                if (!ParseFaction(line.substr(q1 + 1, q2 - q1 - 1), n.faction))
                    continue;
                std::size_t const dash = line.find("-- ");
                n.name = dash == std::string::npos ? "?" : line.substr(dash + 3);
                while (!n.name.empty() && (n.name.back() == '\r' || n.name.back() == ' '))
                    n.name.pop_back();
                out.push_back(n);
            }
            return out;
        }();
        return names;
    }

    // ---- builders ----------------------------------------------------------------------------------------

    Entrant Npc(uint32_t entry, int rank, int stars, int32_t rating = kStartRating)
    {
        Entrant e;
        e.id = NpcId(entry);
        e.name = "Npc" + std::to_string(entry);
        e.rank = rank;
        e.stars = stars;
        e.rating = rating;
        e.bestRank = rank;
        e.bestStars = stars;
        return e;
    }

    Entrant Player(uint32_t account, int rank, int stars, int32_t rating = kStartRating)
    {
        Entrant e = Npc(account, rank, stars, rating);
        e.id = PlayerId(account);
        e.name = "Player" + std::to_string(account);
        return e;
    }

    // n NPCs with entries 1..n, all rank 15, rating 1000.
    LadderState Plain(int n, uint32_t seed = 7)
    {
        LadderState s;
        s.seed = seed;
        for (int i = 1; i <= n; ++i)
            s.entrants.push_back(Npc(static_cast<uint32_t>(i), kBottomRank, 0));
        return s;
    }

    // ---- fake games --------------------------------------------------------------------------------------

    // A fixed strength per id (Elo points, -400 .. 400), so that the ladder has a real order to find.
    double Strength(EntrantId id)
    {
        return static_cast<double>(Mix(static_cast<uint32_t>(id), 99u) % 801u) - 400.0;
    }

    // A fast stand-in for SimulateGame: the result follows the strengths and the game seed; 3 % draws.
    SimResult FakeGame(GamePlan const& g)
    {
        SimResult r;
        r.turns = 10;
        r.firstSide = static_cast<int>(Mix(g.seed, 0xBEEFu) & 1u);
        double const p = 1.0 / (1.0 + std::pow(10.0, (Strength(g.b) - Strength(g.a)) / 400.0));
        if (Mix(g.seed, 0xD2A3u) % 100u < 3u)
            r.outcome = SimOutcome::Draw;
        else
            r.outcome = (Mix(g.seed, 3u) % 100000u) / 100000.0 < p ? SimOutcome::Win0 : SimOutcome::Win1;
        return r;
    }

    GameFn Fake() { return [](GamePlan const& g) { return FakeGame(g); }; }

    // A game function that always lets the first-listed entrant win.
    GameFn AlwaysWin0()
    {
        return [](GamePlan const&) {
            SimResult r;
            r.outcome = SimOutcome::Win0;
            return r;
        };
    }

    // A hand-made one-game round for the state (slot 0: a against b).
    RoundPlan OneGame(LadderState const& s, EntrantId a, EntrantId b, RoundKind kind = RoundKind::Ladder)
    {
        RoundPlan p;
        p.round = s.roundsPlayed + 1;
        p.kind = kind;
        p.seed = RoundSeed(s.seed, p.round);
        GamePlan g;
        g.slot = 0;
        g.a = a;
        g.b = b;
        g.seed = GameSeed(p.seed, 0);
        p.games.push_back(g);
        return p;
    }

    GameResult Res(GamePlan const& g, SimOutcome o)
    {
        GameResult r;
        r.plan = g;
        r.outcome = o;
        if (o == SimOutcome::Failed)
            r.error = "forced";
        return r;
    }

    // Applies one hand-made game with a fixed outcome.
    RoundReport Play(LadderState& s, EntrantId a, EntrantId b, SimOutcome o, RoundKind kind = RoundKind::Ladder)
    {
        RoundPlan const p = OneGame(s, a, b, kind);
        return ApplyRound(s, p, {Res(p.games[0], o)});
    }

    int CountRank(LadderState const& s, int lo, int hi)
    {
        int n = 0;
        for (Entrant const& e : s.entrants)
            if (e.enabled && !e.IsPlayer() && e.rank >= lo && e.rank <= hi)
                ++n;
        return n;
    }

    // ---- tests: stars ------------------------------------------------------------------------------------

    void TestStarBorders()
    {
        // The table of stars per rank: 2 for 25..21, 3 for 20..16, 4 for 15..10, 5 for 9..1.
        for (int r = 25; r >= 21; --r)
            CHECK(MaxStars(r) == 2);
        for (int r = 20; r >= 16; --r)
            CHECK(MaxStars(r) == 3);
        for (int r = 15; r >= 10; --r)
            CHECK(MaxStars(r) == 4);
        for (int r = 9; r >= 1; --r)
            CHECK(MaxStars(r) == 5);
        CHECK(MaxStars(kLegend) == 0);

        // A win inside a rank, and the promotions at full stars: 25 -> 24 needs 2 stars, 16 -> 15, 10 -> 9, 2 -> 1.
        CHECK(StarUp({25, 0}) == (Stars{25, 1}));
        CHECK(StarUp({25, 1}) == (Stars{25, 2}));
        CHECK(StarUp({25, 2}) == (Stars{24, 1}));
        CHECK(StarUp({21, 1}) == (Stars{21, 2}));
        CHECK(StarUp({21, 2}) == (Stars{20, 1}));
        CHECK(StarUp({20, 2}) == (Stars{20, 3}));
        CHECK(StarUp({20, 3}) == (Stars{19, 1}));
        CHECK(StarUp({16, 2}) == (Stars{16, 3}));
        CHECK(StarUp({16, 3}) == (Stars{15, 1}));
        CHECK(StarUp({15, 3}) == (Stars{15, 4}));
        CHECK(StarUp({15, 4}) == (Stars{14, 1}));
        CHECK(StarUp({11, 3}) == (Stars{11, 4}));
        CHECK(StarUp({10, 3}) == (Stars{10, 4}));
        CHECK(StarUp({10, 4}) == (Stars{9, 1}));
        CHECK(StarUp({2, 5}) == (Stars{1, 1}));
        // Rank 1 full stars and a win: Legend. Legend stays Legend.
        CHECK(StarUp({1, 4}) == (Stars{1, 5}));
        CHECK(StarUp({1, 5}) == (Stars{kLegend, 0}));
        CHECK(StarUp({kLegend, 0}) == (Stars{kLegend, 0}));

        // A loss inside a rank, and the demotions at 0 stars: 24 -> 25, 15 -> 16, 14 -> 15, 9 -> 10, 1 -> 2.
        CHECK(StarDown({25, 1}) == (Stars{25, 0}));
        CHECK(StarDown({24, 0}) == (Stars{25, 1}));
        CHECK(StarDown({20, 0}) == (Stars{21, 1}));
        CHECK(StarDown({15, 0}) == (Stars{16, 2}));
        CHECK(StarDown({14, 0}) == (Stars{15, 3}));
        CHECK(StarDown({10, 0}) == (Stars{11, 3}));
        CHECK(StarDown({9, 0}) == (Stars{10, 3}));
        CHECK(StarDown({1, 0}) == (Stars{2, 4}));
        // The floor: rank 25 with 0 stars stays. A loss in Legend costs no star.
        CHECK(StarDown({25, 0}) == (Stars{25, 0}));
        CHECK(StarDown({kLegend, 0}) == (Stars{kLegend, 0}));

        for (int r = 1; r <= kBottomRank; ++r)
            for (int s = 0; s <= MaxStars(r); ++s)
            {
                Stars const up = StarUp({r, s});
                Stars const down = StarDown({r, s});
                CHECK(up.stars >= 0 && up.stars <= MaxStars(up.rank));
                CHECK(down.stars >= 0 && down.stars <= MaxStars(down.rank) && down.rank >= 1 && down.rank <= kBottomRank);
                CHECK(BetterPosition(up, {r, s}));
                CHECK(!BetterPosition(down, {r, s}));
            }
        CHECK(BetterPosition({kLegend, 0}, {1, 5}));
        CHECK(BetterPosition({3, 2}, {3, 1}));
        CHECK(!BetterPosition({3, 2}, {3, 2}));
    }

    // Draws change no stars (player and NPC); the results of a game reach the counters.
    void TestDraw()
    {
        LadderState s = Plain(2);
        s.entrants[0].rank = 12;
        s.entrants[0].stars = 2;
        s.entrants[1].rank = 12;
        s.entrants[1].stars = 2;
        RoundReport const r = Play(s, NpcId(1), NpcId(2), SimOutcome::Draw);
        CHECK(r.ok && r.played == 1);
        CHECK(s.entrants[0].rank == 12 && s.entrants[0].stars == 2 && s.entrants[1].rank == 12 && s.entrants[1].stars == 2);
        CHECK(s.entrants[0].draws == 1 && s.entrants[1].draws == 1 && s.entrants[0].games == 1);
        CHECK(s.entrants[0].rating == kStartRating && s.entrants[1].rating == kStartRating); // equal ratings: no change

        // A draw between unequal ratings moves points from the higher to the lower, zero-sum.
        LadderState u = Plain(2);
        u.entrants[0].rating = 110000;
        Play(u, NpcId(1), NpcId(2), SimOutcome::Draw);
        CHECK(u.entrants[0].rating < 110000 && u.entrants[1].rating > kStartRating);
        CHECK(u.entrants[0].rating + u.entrants[1].rating == 110000 + kStartRating);

        // A player's draw: no stars, but the streak ends.
        LadderState p = Plain(1);
        p.entrants.push_back(Player(1, 10, 2));
        p.entrants[1].winStreak = 2;
        PlayerResultReport const pr = ApplyPlayerResult(p, PlayerId(1), NpcId(1), Result::Draw);
        CHECK(pr.ok && pr.before == pr.after && p.entrants[1].winStreak == 0 && !pr.streakBonus);
        CHECK(p.FindVs(PlayerId(1), NpcId(1)) && p.FindVs(PlayerId(1), NpcId(1))->draws == 1);
    }

    // ---- tests: streak bonus -----------------------------------------------------------------------------

    void TestStreakBonus()
    {
        LadderState s = Plain(1);
        s.entrants.push_back(Player(1, 10, 0));
        auto win = [&]() { return ApplyPlayerResult(s, PlayerId(1), NpcId(1), Result::Win); };
        auto loss = [&]() { return ApplyPlayerResult(s, PlayerId(1), NpcId(1), Result::Loss); };
        Entrant const& p = *s.Find(PlayerId(1));

        PlayerResultReport r = win();
        CHECK(r.after == (Stars{10, 1}) && !r.streakBonus && r.winStreak == 1);
        r = win();
        CHECK(r.after == (Stars{10, 2}) && !r.streakBonus && r.winStreak == 2);
        r = win(); // the third win in a row from rank 10: a bonus star
        CHECK(r.after == (Stars{10, 4}) && r.streakBonus && r.winStreak == 3);
        r = win(); // the fourth: the bonus star carries over the promotion (10 holds 4 stars)
        CHECK(r.after == (Stars{9, 2}) && r.streakBonus && r.winStreak == 4);
        r = loss(); // a loss resets the streak
        CHECK(r.after == (Stars{9, 1}) && p.winStreak == 0);
        r = win();
        r = win();
        CHECK(r.after == (Stars{9, 3}) && !r.streakBonus && p.winStreak == 2);
        // A draw also resets it.
        ApplyPlayerResult(s, PlayerId(1), NpcId(1), Result::Draw);
        CHECK(p.winStreak == 0);
        r = win();
        CHECK(!r.streakBonus && p.winStreak == 1);

        // The bonus needs a rank worse than 5: rank 6 yes (and the promoting star still carries it), rank 5 and
        // above no.
        auto at = [&](int rank, int stars, int streak) {
            s.Find(PlayerId(1))->rank = rank;
            s.Find(PlayerId(1))->stars = stars;
            s.Find(PlayerId(1))->winStreak = streak;
        };
        at(6, 5, 2);
        r = win();
        CHECK(r.streakBonus && r.after == (Stars{5, 2}));
        at(6, 1, 9);
        r = win();
        CHECK(r.streakBonus && r.after == (Stars{6, 3}));
        at(5, 1, 2);
        r = win();
        CHECK(!r.streakBonus && r.after == (Stars{5, 2}) && p.winStreak == 3);
        at(5, 1, 20);
        r = win();
        CHECK(!r.streakBonus && r.after == (Stars{5, 2}));
        at(1, 3, 6);
        r = win();
        CHECK(!r.streakBonus && r.after == (Stars{1, 4}));
        at(kBottomRank, 0, 2);
        r = win(); // the lowest rank gets it too (25 holds 2 stars)
        CHECK(r.streakBonus && r.after == (Stars{kBottomRank, 2}));

        // Rank 1 full stars in a streak enters Legend without a bonus (and does not skip into anything).
        at(1, 5, 2);
        r = win();
        CHECK(r.after == (Stars{kLegend, 0}) && !r.streakBonus);

        // NPCs never get it: five wins in a row are exactly five stars, and they keep no streak.
        LadderState n = Plain(2);
        n.entrants[0].rank = 9; // 5 stars
        for (int i = 0; i < 5; ++i)
            Play(n, NpcId(1), NpcId(2), SimOutcome::Win0);
        CHECK(n.entrants[0].rank == 9 && n.entrants[0].stars == 5 && n.entrants[0].winStreak == 0);
        // The NPC that beats a player in a streak does not get a bonus either, and a player's NPC opponent loses a
        // star as usual.
        LadderState q = Plain(1);
        q.entrants[0].rank = 10;
        q.entrants[0].stars = 3;
        q.entrants.push_back(Player(1, 10, 0));
        for (int i = 0; i < 4; ++i)
            ApplyPlayerResult(q, PlayerId(1), NpcId(1), Result::Win);
        CHECK(q.entrants[0].rank == 11 && q.entrants[0].stars == 3); // 3 stars, four losses: 2,1,0, then a demotion to 11 with 3 of 4 stars
    }

    // ---- tests: Legend -----------------------------------------------------------------------------------

    // Ten NPCs (entries 20..29) in Legend with the ratings base + 100 * i.
    LadderState LegendFull(int32_t base = 105000)
    {
        LadderState s;
        s.seed = 3;
        for (uint32_t i = 0; i < 10; ++i)
            s.entrants.push_back(Npc(20 + i, kLegend, 0, base + 100 * static_cast<int32_t>(i)));
        return s;
    }

    void TestLegendPushOutNpc()
    {
        // An 11th NPC reaches Legend with a higher rating than the weakest member: that member goes to 1 / 4 stars.
        LadderState s = LegendFull();
        s.entrants.push_back(Npc(1, 1, 5, 104000));
        s.entrants.push_back(Npc(2, 5, 0, 104000));
        RoundReport r = Play(s, NpcId(1), NpcId(2), SimOutcome::Win0);
        CHECK(r.ok && s.Find(NpcId(1))->rank == kLegend);
        CHECK(r.pushedOut.size() == 1 && r.pushedOut[0] == NpcId(20));
        CHECK(s.Find(NpcId(20))->rank == 1 && s.Find(NpcId(20))->stars == 4);
        CHECK(CountRank(s, 0, 0) == 10);
        CHECK(s.Find(NpcId(20))->bestRank == kLegend); // it had been there

        // The newcomer is the weakest: it is the one sent back, at once (and has not "reached" Legend).
        s = LegendFull();
        s.entrants.push_back(Npc(1, 1, 5, 90000));
        s.entrants.push_back(Npc(2, 5, 0, 90000));
        r = Play(s, NpcId(1), NpcId(2), SimOutcome::Win0);
        CHECK(r.pushedOut.size() == 1 && r.pushedOut[0] == NpcId(1));
        CHECK(s.Find(NpcId(1))->rank == 1 && s.Find(NpcId(1))->stars == 4 && s.Find(NpcId(1))->bestRank == 1);
        CHECK(CountRank(s, 0, 0) == 10);

        // Exact tie on the lowest rating: the higher id leaves. Newcomer rating 104000 + 1600 = 105600 equals ...
        s = LegendFull(105600);
        s.entrants.push_back(Npc(5, 1, 5, 105600 - 1600)); // wins against an equal-rated opponent: +1600
        s.entrants.push_back(Npc(6, 5, 0, 105600 - 1600));
        r = Play(s, NpcId(5), NpcId(6), SimOutcome::Win0);
        CHECK(s.Find(NpcId(5))->rating == 105600 && s.Find(NpcId(20))->rating == 105600);
        CHECK(r.pushedOut.size() == 1 && r.pushedOut[0] == NpcId(20)); // 5 < 20: 20 is the last in ladder order
        CHECK(s.Find(NpcId(5))->rank == kLegend);

        // Two arrivals in one round: checked after the whole round, the two weakest leave.
        s = LegendFull();
        s.entrants.push_back(Npc(1, 1, 5, 120000));
        s.entrants.push_back(Npc(2, 1, 5, 120000));
        s.entrants.push_back(Npc(3, 5, 0, 120000));
        s.entrants.push_back(Npc(4, 5, 0, 120000));
        RoundPlan plan = OneGame(s, NpcId(1), NpcId(3));
        GamePlan g2 = plan.games[0];
        g2.slot = 1;
        g2.a = NpcId(2);
        g2.b = NpcId(4);
        g2.seed = GameSeed(plan.seed, 1);
        plan.games.push_back(g2);
        r = ApplyRound(s, plan, {Res(plan.games[0], SimOutcome::Win0), Res(plan.games[1], SimOutcome::Win0)});
        CHECK(r.ok && r.pushedOut.size() == 2 && CountRank(s, 0, 0) == 10);
        CHECK(s.Find(NpcId(20))->rank == 1 && s.Find(NpcId(21))->rank == 1 && s.Find(NpcId(1))->rank == kLegend);

        // A loss in Legend costs no star, only hidden rating.
        s = LegendFull();
        s.entrants.push_back(Npc(2, 5, 0));
        Play(s, NpcId(2), NpcId(29), SimOutcome::Win0);
        CHECK(s.Find(NpcId(29))->rank == kLegend && s.Find(NpcId(29))->stars == 0 && s.Find(NpcId(29))->rating < 105900);
        CHECK(s.Find(NpcId(2))->stars == 1);
        // ... and a smaller Legend limit is a parameter.
        LadderRules three;
        three.legendSize = 3;
        std::vector<EntrantId> const pushed = EnforceLegendLimit(s, three);
        CHECK(pushed.size() == 7 && CountRank(s, 0, 0) == 3);
    }

    void TestLegendPushOutPlayer()
    {
        // The player wins into Legend: the weakest NPC leaves.
        LadderState s = LegendFull();
        s.entrants.push_back(Npc(1, 5, 0, 106000));
        s.entrants.push_back(Player(1, 1, 5, 106000));
        PlayerResultReport r = ApplyPlayerResult(s, PlayerId(1), NpcId(1), Result::Win);
        CHECK(r.ok && r.after.rank == kLegend && r.newBest);
        CHECK(r.pushedOut.size() == 1 && r.pushedOut[0] == NpcId(20));
        CHECK(s.Find(PlayerId(1))->bestRank == kLegend);
        CHECK(CountRank(s, 0, 0) == 9); // NPCs: the player holds the tenth place
        CHECK(s.Find(NpcId(20))->rank == 1 && s.Find(NpcId(20))->stars == 4);

        // The player is the weakest after the win: sent back at once. That does not count as reaching Legend.
        s = LegendFull(150000);
        s.entrants.push_back(Npc(1, 5, 0, 100000));
        s.entrants.push_back(Player(1, 1, 5, 100000));
        r = ApplyPlayerResult(s, PlayerId(1), NpcId(1), Result::Win);
        CHECK(r.ok && r.after == (Stars{1, 4}) && r.pushedOut.size() == 1 && r.pushedOut[0] == PlayerId(1));
        CHECK(s.Find(PlayerId(1))->bestRank == 1 && !r.newBest);
        CHECK(CountRank(s, 0, 0) == 10);

        // A loss in Legend: no star, only rating; the weakest Legend member is next to leave.
        s = LegendFull();
        s.entrants.pop_back(); // nine NPCs, the player holds the tenth place
        s.entrants.push_back(Npc(1, 5, 0));
        s.entrants.push_back(Player(1, kLegend, 0, 130000));
        r = ApplyPlayerResult(s, PlayerId(1), NpcId(1), Result::Loss);
        CHECK(r.ok && r.after.rank == kLegend && r.ratingDelta < 0 && s.Find(PlayerId(1))->rating == 130000 + r.ratingDelta);
        CHECK(r.pushedOut.empty() && s.Find(PlayerId(1))->stars == 0 && s.Find(NpcId(1))->stars == 1);
    }

    // ---- tests: hidden rating ----------------------------------------------------------------------------

    void TestElo()
    {
        // Equal ratings: 16 points for a decisive result, nothing for a draw.
        CHECK(EloDeltaA(100000, 100000, Result::Win) == 1600);
        CHECK(EloDeltaA(100000, 100000, Result::Loss) == -1600);
        CHECK(EloDeltaA(100000, 100000, Result::Draw) == 0);
        // 1000 beats 1200: about 24.3; beats 800: about 7.7 (rounded to a centi-point, half away from zero).
        CHECK(EloDeltaA(100000, 120000, Result::Win) == 2431);
        CHECK(EloDeltaA(100000, 80000, Result::Win) == 769);
        CHECK(EloDeltaA(100000, 120000, Result::Loss) == -769);
        CHECK(EloDeltaA(100000, 80000, Result::Loss) == -2431);
        // A draw takes from the stronger: 1200 vs 1000 -> -8.1 for the stronger, +8.1 for the weaker.
        CHECK(EloDeltaA(120000, 100000, Result::Draw) == -(EloDeltaA(100000, 120000, Result::Draw)));
        CHECK(EloDeltaA(120000, 100000, Result::Draw) < 0 && EloDeltaA(120000, 100000, Result::Draw) > -1600);

        // Extremes: the favourite gets nothing, the underdog the whole K, never more.
        CHECK(EloDeltaA(100000, 10000000, Result::Win) == 3200);
        CHECK(EloDeltaA(10000000, 100000, Result::Loss) == -3200);
        CHECK(EloDeltaA(10000000, 100000, Result::Win) == 0);
        CHECK(EloDeltaA(100000, 10000000, Result::Loss) == 0);
        CHECK(EloDeltaA(0, 0, Result::Win) == 1600);
        CHECK(EloDeltaA(-50000, 2000000, Result::Draw) > 1590 && EloDeltaA(-50000, 2000000, Result::Draw) <= 1600);

        // Over a sweep: a win is always worth between 0 and K, win and loss add up to K (within a centi-point),
        // and a better opponent is always worth more.
        int32_t last = 0;
        bool monotonic = true;
        bool bounded = true;
        for (int32_t b = 20000; b <= 200000; b += 777)
        {
            int32_t const w = EloDeltaA(100000, b, Result::Win);
            int32_t const l = EloDeltaA(100000, b, Result::Loss);
            bounded = bounded && w >= 0 && w <= 3200 && l <= 0 && l >= -3200 && std::abs(w - l - 3200) <= 1;
            monotonic = monotonic && w >= last;
            last = w;
        }
        CHECK(bounded && monotonic);

        // Zero-sum through the applied games: the sum of all ratings never changes.
        LadderState s = Plain(20, 11);
        int64_t const total = 20ll * kStartRating;
        for (int round = 0; round < 40; ++round)
        {
            RoundOptions o;
            o.kind = round % 2 == 0 ? RoundKind::RatingOnly : RoundKind::Ladder;
            RoundReport const r = RunRound(s, Fake(), 1, o);
            CHECK(r.ok);
        }
        int64_t sum = 0;
        int32_t lowest = kStartRating, highest = kStartRating;
        for (Entrant const& e : s.entrants)
        {
            sum += e.rating;
            lowest = std::min(lowest, e.rating);
            highest = std::max(highest, e.rating);
        }
        CHECK(sum == total);
        CHECK(lowest < kStartRating && highest > kStartRating); // the ratings really spread
    }

    // ---- tests: ladder order -----------------------------------------------------------------------------

    void TestLadderOrder()
    {
        LadderState s;
        s.entrants.push_back(Npc(5, 3, 2, 100000));
        s.entrants.push_back(Npc(4, 3, 2, 100000)); // exact tie with 5 and the player: the id decides
        s.entrants.push_back(Player(1, 3, 2, 100000));
        s.entrants.push_back(Npc(6, 3, 2, 110000));
        s.entrants.push_back(Npc(7, 3, 4, 90000));
        s.entrants.push_back(Npc(10, 2, 0, 90000));
        s.entrants.push_back(Npc(9, kLegend, 0, 110000));
        s.entrants.push_back(Npc(8, kLegend, 0, 120000));
        s.entrants.push_back(Npc(11, 1, 5, 150000));
        s.entrants.back().enabled = false; // left the roster: not in the order
        std::vector<Entrant const*> const order = LadderOrder(s);
        std::vector<EntrantId> const want = {NpcId(8), NpcId(9), NpcId(10), NpcId(7), NpcId(6), NpcId(4), NpcId(5), PlayerId(1)};
        CHECK(order.size() == want.size());
        for (std::size_t i = 0; i < want.size() && i < order.size(); ++i)
            CHECK(order[i]->id == want[i]);

        // Legend is ordered by hidden rating alone; an exact tie there goes to the lower id.
        LadderState l;
        l.entrants.push_back(Npc(3, kLegend, 0, 100000));
        l.entrants.push_back(Npc(2, kLegend, 0, 100000));
        l.entrants.push_back(Npc(1, kLegend, 0, 99999));
        std::vector<Entrant const*> const lo = LadderOrder(l);
        CHECK(lo[0]->id == NpcId(2) && lo[1]->id == NpcId(3) && lo[2]->id == NpcId(1));
        CHECK(!LadderLess(*lo[0], *lo[0]));
        CHECK(LadderLess(*lo[0], *lo[1]) && !LadderLess(*lo[1], *lo[0]));
        // Any number of player entries share the order; the numbers keep them apart.
        LadderState two;
        two.entrants.push_back(Player(2, 8, 1));
        two.entrants.push_back(Player(1, 8, 1));
        CHECK(LadderOrder(two)[0]->id == PlayerId(1));
        CHECK(IsPlayerId(PlayerId(0)) && !IsPlayerId(NpcId(4000000000u)));
    }

    // ---- tests: roster -----------------------------------------------------------------------------------

    int DeckCount(std::vector<LibraryDeck> const& lib, std::string const& cls, DeckRating rating)
    {
        int n = 0;
        for (LibraryDeck const& d : lib)
            n += d.heroClass == cls && d.rating == rating ? 1 : 0;
        return n;
    }

    LibraryDeck const* DeckOf(std::vector<LibraryDeck> const& lib, std::string const& key)
    {
        for (LibraryDeck const& d : lib)
            if (d.key == key)
                return &d;
        return nullptr;
    }

    void TestRosterRealNames()
    {
        std::vector<LibraryDeck> const& lib = Library();
        std::vector<FinderName> names = Finder();
        std::sort(names.begin(), names.end(), [](FinderName const& a, FinderName const& b) { return a.entry < b.entry; });
        CHECK(names.size() == 101);
        CHECK(lib.size() == 238);
        // Every class has Hard and Easy library decks (the Naxx list gives Druid and Rogue Easy decks too).
        for (char const* cls : kClasses)
        {
            CHECK(DeckCount(lib, cls, DeckRating::Hard) > 0);
            CHECK(DeckCount(lib, cls, DeckRating::Easy) > 0);
        }

        LadderState s;
        s.seed = 4242;
        RosterChange const c = UpdateRoster(s, names, lib);
        CHECK(c.added.size() == 101 && c.disabled.empty() && c.enabled.empty() && c.unassigned.empty());
        CHECK(s.entrants.size() == 101);

        std::map<std::string, int> perClass;
        std::map<std::string, int> perClassSlot[4];
        std::map<std::string, int> deckUse;
        bool allNew = true, deckOk = true, factionOk = true;
        for (std::size_t i = 0; i < s.entrants.size(); ++i)
        {
            Entrant const& e = s.entrants[i];
            allNew = allNew && e.rank == kBottomRank && e.stars == 0 && e.rating == kStartRating && e.enabled &&
                     e.games == 0 && e.joinedRound == 0;
            factionOk = factionOk && e.id == NpcId(names[i].entry) && e.faction == names[i].faction &&
                        e.name == names[i].name;
            LibraryDeck const* d = DeckOf(lib, e.deckKey);
            deckOk = deckOk && d && d->heroClass == e.heroClass;
            if (!d)
                continue;
            ++perClass[e.heroClass];
            ++deckUse[e.deckKey];
            for (int m = 0; m < 4; ++m)
                if (kMix[m].profile == e.profile && kMix[m].rating == d->rating)
                    ++perClassSlot[m][e.heroClass];
        }
        CHECK(allNew && deckOk && factionOk);

        // Classes: 101 = 9 * 11 + 2.
        int lo = 1000, hi = 0;
        for (char const* cls : kClasses)
        {
            lo = std::min(lo, perClass[cls]);
            hi = std::max(hi, perClass[cls]);
        }
        CHECK(perClass.size() == 9 && lo == 11 && hi == 12);

        // The mix: within a class, the four combinations (two for Druid and Rogue) are used equally often (+-1).
        for (char const* cls : kClasses)
        {
            int mn = 1000, mx = 0;
            for (int m = 0; m < 4; ++m)
            {
                mn = std::min(mn, perClassSlot[m][cls]);
                mx = std::max(mx, perClassSlot[m][cls]);
            }
            CHECK(mn >= 1 && mx - mn <= 1);
        }

        // Decks: the least used first, so within a class and rating the decks are used equally often (+-1); a
        // deck is never reused while another of its class and rating is unused.
        bool evenDecks = true;
        for (char const* cls : kClasses)
            for (DeckRating rating : {DeckRating::Easy, DeckRating::Hard})
            {
                int mn = 1000, mx = 0;
                for (LibraryDeck const& d : lib)
                    if (d.heroClass == cls && d.rating == rating)
                    {
                        mn = std::min(mn, deckUse[d.key]);
                        mx = std::max(mx, deckUse[d.key]);
                    }
                evenDecks = evenDecks && (mx == 0 || mx - mn <= 1);
            }
        CHECK(evenDecks);

        // Existing assignments never change: a second run with the same list does nothing.
        uint64_t const h = StateHash(s);
        RosterChange const again = UpdateRoster(s, names, lib);
        CHECK(again.added.empty() && again.disabled.empty() && again.enabled.empty() && again.unassigned.empty());
        CHECK(StateHash(s) == h);

        // The input order does not matter; the ladder seed does.
        std::vector<FinderName> reversed(names.rbegin(), names.rend());
        LadderState r;
        r.seed = 4242;
        UpdateRoster(r, reversed, lib);
        CHECK(StateHash(r) == h);
        LadderState other;
        other.seed = 4243;
        UpdateRoster(other, names, lib);
        CHECK(StateHash(other) != h);
        // A league reset is a new ladder with a new seed: most NPCs get another deck, everybody starts at the bottom rank.
        std::size_t changedDecks = 0;
        bool bottom = true;
        for (std::size_t i = 0; i < s.entrants.size() && i < other.entrants.size(); ++i)
        {
            changedDecks += s.entrants[i].deckKey != other.entrants[i].deckKey ? 1 : 0;
            bottom = bottom && other.entrants[i].rank == kBottomRank && other.entrants[i].stars == 0;
        }
        CHECK(changedDecks > 50 && bottom && !other.started && other.roundsPlayed == 0);

        // A new name joins at the bottom with a fresh record; nobody else changes.
        s.roundsPlayed = 9;
        s.started = true;
        s.Find(NpcId(names[0].entry))->rank = 4;
        s.Find(NpcId(names[0].entry))->wins = 6;
        std::vector<Entrant> const before = s.entrants;
        std::vector<FinderName> grown = names;
        FinderName extra;
        extra.entry = 999001;
        extra.faction = Faction::Horde;
        extra.name = "Newcomer";
        grown.push_back(extra);
        RosterChange const joined = UpdateRoster(s, grown, lib);
        CHECK(joined.added.size() == 1 && joined.added[0] == NpcId(999001) && joined.disabled.empty());
        Entrant const* nw = s.Find(NpcId(999001));
        CHECK(nw && nw->rank == kBottomRank && nw->stars == 0 && nw->rating == kStartRating && nw->joinedRound == 9 &&
              nw->faction == Faction::Horde && nw->enabled && !nw->deckKey.empty());
        bool untouched = s.entrants.size() == before.size() + 1;
        for (std::size_t i = 0; untouched && i < before.size(); ++i)
            untouched = s.entrants[i].id == before[i].id && s.entrants[i].deckKey == before[i].deckKey &&
                        s.entrants[i].heroClass == before[i].heroClass && s.entrants[i].profile == before[i].profile &&
                        s.entrants[i].rank == before[i].rank && s.entrants[i].wins == before[i].wins;
        CHECK(untouched);

        // A name that leaves is disabled, keeps its record and its assignment, and is out of the order and the
        // rounds; when it returns it is enabled again with the old record.
        std::vector<FinderName> shrunk;
        for (FinderName const& n : names)
            if (n.entry != names[0].entry)
                shrunk.push_back(n);
        RosterChange const left = UpdateRoster(s, shrunk, lib);
        // (the newcomer was not in the shrunk list either)
        CHECK(left.disabled.size() == 2 && left.disabled[0] == NpcId(names[0].entry) && left.disabled[1] == NpcId(999001));
        Entrant const* gone = s.Find(NpcId(names[0].entry));
        CHECK(gone && !gone->enabled && gone->rank == 4 && gone->wins == 6 && gone->deckKey == before[0].deckKey);
        bool inOrder = false;
        for (Entrant const* e : LadderOrder(s))
            inOrder = inOrder || e->id == NpcId(names[0].entry);
        CHECK(!inOrder);
        RoundPlan const plan = PlanRound(s);
        bool inRound = plan.bye == NpcId(names[0].entry);
        for (GamePlan const& g : plan.games)
            inRound = inRound || g.a == NpcId(names[0].entry) || g.b == NpcId(names[0].entry);
        CHECK(!inRound);
        RosterChange const back = UpdateRoster(s, grown, lib);
        CHECK(back.enabled.size() == 2 && s.Find(NpcId(names[0].entry))->enabled && s.Find(NpcId(names[0].entry))->rank == 4);
        CHECK(s.Find(NpcId(names[0].entry))->deckKey == before[0].deckKey);

        // A library without decks assigns nothing and reports the names.
        LadderState empty;
        RosterChange const none = UpdateRoster(empty, names, std::vector<LibraryDeck>());
        CHECK(none.added.empty() && none.unassigned.size() == 101 && empty.entrants.empty());
    }

    // A one-class library shows the exact rotation and the deck rotation.
    void TestRosterRotation()
    {
        auto deck = [](char const* key, char const* cls, DeckRating rating) {
            LibraryDeck d;
            d.key = key;
            d.heroClass = cls;
            d.rating = rating;
            return d;
        };
        std::vector<LibraryDeck> lib = {deck("m_e1", "MAGE", DeckRating::Easy), deck("m_e2", "MAGE", DeckRating::Easy),
                                        deck("m_h1", "MAGE", DeckRating::Hard), deck("m_h2", "MAGE", DeckRating::Hard)};
        std::vector<FinderName> names;
        for (uint32_t i = 1; i <= 9; ++i)
        {
            FinderName n;
            n.entry = i * 10;
            n.name = "N" + std::to_string(i);
            names.push_back(n);
        }
        LadderState s;
        UpdateRoster(s, names, lib);
        // Rotation: (Normal, easy), (Hard, hard), (Normal, hard), (Hard, easy), then again.
        for (std::size_t i = 0; i < 9; ++i)
        {
            Entrant const& e = s.entrants[i];
            CHECK(e.heroClass == "MAGE");
            CHECK(e.profile == kMix[i % 4].profile);
            CHECK(DeckOf(lib, e.deckKey)->rating == kMix[i % 4].rating);
        }
        // Least used deck first: nine NPCs, five with easy decks (3 + 2) and four with hard ones (2 + 2).
        std::map<std::string, int> use;
        for (Entrant const& e : s.entrants)
            ++use[e.deckKey];
        CHECK(use["m_h1"] == 2 && use["m_h2"] == 2 && use["m_e1"] + use["m_e2"] == 5 && std::abs(use["m_e1"] - use["m_e2"]) == 1);

        // Hard decks only (Druid / Rogue): only the two hard combinations rotate.
        std::vector<LibraryDeck> hardLib = {deck("d_h1", "DRUID", DeckRating::Hard), deck("d_h2", "DRUID", DeckRating::Hard),
                                            deck("d_h3", "DRUID", DeckRating::Hard)};
        LadderState d;
        UpdateRoster(d, names, hardLib);
        for (std::size_t i = 0; i < 9; ++i)
            CHECK(d.entrants[i].profile == (i % 2 == 0 ? Match::AiProfile::Hard : Match::AiProfile::Normal));
        std::map<std::string, int> dUse;
        for (Entrant const& e : d.entrants)
            ++dUse[e.deckKey];
        CHECK(dUse["d_h1"] == 3 && dUse["d_h2"] == 3 && dUse["d_h3"] == 3);

        // Existing NPCs count: three Mages already there, then the new names go to the other classes first.
        std::vector<LibraryDeck> two = {deck("m_e1", "MAGE", DeckRating::Easy), deck("w_e1", "WARRIOR", DeckRating::Easy),
                                        deck("p_e1", "PRIEST", DeckRating::Easy)};
        LadderState c;
        for (uint32_t i = 100; i < 103; ++i)
        {
            Entrant m = Npc(i, kBottomRank, 0);
            m.heroClass = "MAGE";
            m.deckKey = "m_e1";
            c.entrants.push_back(m);
        }
        UpdateRoster(c, names, two); // the three Mages are not in the finder list: disabled, so not counted
        std::map<std::string, int> cls;
        for (Entrant const& e : c.entrants)
            if (e.enabled)
                ++cls[e.heroClass];
        CHECK(cls["MAGE"] == 3 && cls["WARRIOR"] == 3 && cls["PRIEST"] == 3);
        // The same with the Mages still on the list: they are counted, so the others fill up to them.
        LadderState k;
        for (uint32_t i = 1; i <= 3; ++i)
        {
            Entrant m = Npc(i * 10, kBottomRank, 0);
            m.heroClass = "MAGE";
            m.deckKey = "m_e1";
            k.entrants.push_back(m);
        }
        std::vector<FinderName> more = names;
        more.resize(7);
        UpdateRoster(k, more, two);
        std::map<std::string, int> kc;
        for (Entrant const& e : k.entrants)
            ++kc[e.heroClass];
        CHECK(kc["MAGE"] == 3 && kc["WARRIOR"] == 2 && kc["PRIEST"] == 2);
    }

    // ---- tests: ladder start -----------------------------------------------------------------------------

    void TestPlacement()
    {
        int const sizes[] = {0, 1, 2, 3, 9, 10, 11, 12, 14, 15, 16, 25, 26, 40, 101, 200};
        for (int n : sizes)
        {
            LadderState s;
            for (int i = 1; i <= n; ++i)
                s.entrants.push_back(Npc(static_cast<uint32_t>(i), kBottomRank, 0, 50000 + (i * 7919) % 100003));
            Entrant disabled = Npc(5000, kBottomRank, 0, 999999); // never placed
            disabled.enabled = false;
            s.entrants.push_back(disabled);
            s.entrants.push_back(Player(1, 12, 1));
            PlaceNpcs(s);
            CHECK(s.started);
            CHECK(s.Find(NpcId(5000))->rank == kBottomRank && s.Find(PlayerId(1))->rank == 12 && s.Find(PlayerId(1))->stars == 1);

            std::vector<Entrant const*> byRating;
            for (Entrant const& e : s.entrants)
                if (!e.IsPlayer() && e.enabled)
                    byRating.push_back(&e);
            std::sort(byRating.begin(), byRating.end(), [](Entrant const* a, Entrant const* b) {
                return a->rating != b->rating ? a->rating > b->rating : a->id < b->id;
            });
            std::size_t const legend = std::min<std::size_t>(10, byRating.size());
            std::size_t const rest = byRating.size() - legend;
            bool legendOk = true, monotonic = true, starsOk = true;
            int prev = 1;
            int perRank[kBottomRank + 1] = {};
            for (std::size_t i = 0; i < byRating.size(); ++i)
            {
                Entrant const& e = *byRating[i];
                starsOk = starsOk && e.stars == 0;
                if (i < legend)
                {
                    legendOk = legendOk && e.rank == kLegend;
                    continue;
                }
                monotonic = monotonic && e.rank >= prev && e.rank >= 1 && e.rank <= kBottomRank;
                prev = e.rank;
                ++perRank[e.rank];
            }
            CHECK(legendOk && monotonic && starsOk);
            if (rest > 0)
            {
                CHECK(byRating[legend]->rank == 1); // the best of the rest at rank 1
                int mn = 1000, mx = 0, used = 0;
                for (int r = 1; r <= kBottomRank; ++r)
                {
                    mn = std::min(mn, perRank[r]);
                    mx = std::max(mx, perRank[r]);
                    used += perRank[r] > 0 ? 1 : 0;
                }
                if (rest >= static_cast<std::size_t>(kBottomRank))
                    CHECK(mn >= 1 && mx - mn <= 1);
                else
                    CHECK(mx == 1 && used == static_cast<int>(rest)); // spread out, no two on one rank
            }
            CHECK(CountRank(s, 0, 0) == static_cast<int>(legend));
        }

        // The exact numbers for the odd cases.
        LadderState two;
        for (uint32_t i = 1; i <= 12; ++i)
            two.entrants.push_back(Npc(i, kBottomRank, 0, 100000 + 100 * static_cast<int32_t>(i)));
        PlaceNpcs(two);
        CHECK(two.Find(NpcId(12))->rank == 0 && two.Find(NpcId(3))->rank == 0);
        CHECK(two.Find(NpcId(2))->rank == 1 && two.Find(NpcId(1))->rank == 13); // 2 left: ranks 1 and 13
        LadderState one;
        one.entrants.push_back(Npc(1, kBottomRank, 0));
        PlaceNpcs(one);
        CHECK(one.Find(NpcId(1))->rank == kLegend); // fewer NPCs than places: all in Legend
        LadderState big;
        for (uint32_t i = 1; i <= 101; ++i)
            big.entrants.push_back(Npc(i, kBottomRank, 0, 100000 + 10 * static_cast<int32_t>(i)));
        PlaceNpcs(big);
        int three = 0, four = 0;
        for (int r = 1; r <= kBottomRank; ++r)
        {
            int const n = CountRank(big, r, r);
            three += n == 3 ? 1 : 0;
            four += n == 4 ? 1 : 0;
        }
        CHECK(three == 9 && four == 16 && CountRank(big, 0, 0) == 10); // 91 = 9 * 3 + 16 * 4
        // A smaller Legend is a parameter.
        LadderRules rules;
        rules.legendSize = 3;
        LadderState small = big;
        for (Entrant& e : small.entrants)
        {
            e.rank = kBottomRank;
            e.stars = 0;
        }
        PlaceNpcs(small, rules);
        CHECK(CountRank(small, 0, 0) == 3);
    }

    // The ladder start on the 101 finder names with the fake game: 20 rating-only rounds, then the placement.
    void TestLadderStartFake()
    {
        LadderState s;
        s.seed = 20261014;
        UpdateRoster(s, Finder(), Library());
        // The fake strengths follow the creature entry (Strength(id)), the roster does not need decks for it.
        int callbacks = 0;
        int const played = RunLadderStart(s, Fake(), 4, LadderRules(),
            [&](LadderState const& st, RoundReport const& r) {
                ++callbacks;
                CHECK(r.ok && st.roundsPlayed == r.round && !st.started);
            });
        CHECK(played == 20 && callbacks == 20 && s.roundsPlayed == 20 && s.started);

        // Only hidden ratings changed during the rounds; every NPC played one game per round except its bye.
        bool countersOk = true;
        int64_t sum = 0;
        for (Entrant const& e : s.entrants)
        {
            countersOk = countersOk && e.games + e.byes == 20 && e.wins + e.losses + e.draws == e.games;
            sum += e.rating;
        }
        CHECK(countersOk && sum == 101ll * kStartRating);
        // 101 is odd: 20 rounds, 20 byes, fairly distributed.
        uint32_t byeSum = 0, byeMax = 0;
        for (Entrant const& e : s.entrants)
        {
            byeSum += e.byes;
            byeMax = std::max(byeMax, e.byes);
        }
        CHECK(byeSum == 20 && byeMax == 1);

        // The placement: the ten best ratings in Legend, an even spread below.
        CHECK(CountRank(s, 0, 0) == 10);
        int32_t legendLowest = 1 << 30, otherHighest = 0;
        for (Entrant const& e : s.entrants)
        {
            if (e.rank == kLegend)
                legendLowest = std::min(legendLowest, e.rating);
            else
                otherHighest = std::max(otherHighest, e.rating);
        }
        CHECK(legendLowest >= otherHighest); // the ten best hidden ratings
        int mn = 1000, mx = 0;
        for (int r = 1; r <= kBottomRank; ++r)
        {
            mn = std::min(mn, CountRank(s, r, r));
            mx = std::max(mx, CountRank(s, r, r));
        }
        CHECK(mn == 3 && mx == 4);
        // The ratings found the strong: the Legend NPCs are clearly stronger than the field.
        double legendStrength = 0, allStrength = 0;
        for (Entrant const& e : s.entrants)
        {
            allStrength += Strength(e.id) / 101.0;
            if (e.rank == kLegend)
                legendStrength += Strength(e.id) / 10.0;
        }
        CHECK(legendStrength > allStrength + 100.0);
        std::printf("    ladder start: Legend strength %.0f vs field %.0f\n", legendStrength, allStrength);

        // A start that is already done is not repeated.
        uint64_t const h = StateHash(s);
        CHECK(RunLadderStart(s, Fake(), 1) == 0 && StateHash(s) == h);

        // Small and odd rosters, and a start without rounds.
        for (int n : {0, 1, 2, 3, 11, 12})
        {
            LadderState t = Plain(n, 5);
            int const rounds = RunLadderStart(t, Fake(), 2);
            CHECK(rounds == 20 && t.started && t.roundsPlayed == 20);
            CHECK(CountRank(t, 0, 0) == std::min(n, 10));
            CHECK(CountRank(t, 1, kBottomRank) == std::max(0, n - 10));
        }
        LadderRules none;
        none.startRounds = 0;
        LadderState z = Plain(14, 5);
        CHECK(RunLadderStart(z, Fake(), 1, none) == 0 && z.started && CountRank(z, 0, 0) == 10);

        // Interrupted start: a copy of the state saved after 7 rounds continues to the same ladder.
        LadderState saved;
        LadderState straight = Plain(23, 77);
        RunLadderStart(straight, Fake(), 1);
        LadderState crashed = Plain(23, 77);
        for (int i = 0; i < 7; ++i)
        {
            RoundOptions o;
            o.kind = RoundKind::RatingOnly;
            RunRound(crashed, Fake(), 1, o);
        }
        saved = crashed; // what the glue had stored
        RunLadderStart(saved, Fake(), 3);
        CHECK(StateHash(saved) == StateHash(straight));
    }

    // ---- tests: pairing ----------------------------------------------------------------------------------

    std::string PlanKey(RoundPlan const& p)
    {
        std::ostringstream os;
        os << p.round << '/' << static_cast<int>(p.kind) << '/' << p.seed << '/' << p.excluded << '/' << p.bye;
        for (GamePlan const& g : p.games)
            os << '|' << g.slot << ':' << g.a << ':' << g.b << ':' << g.seed << ':' << g.firstSide;
        return os.str();
    }

    void TestPairing()
    {
        int const sizes[] = {0, 1, 2, 3, 7, 8, 9, 20, 101};
        for (RoundKind kind : {RoundKind::RatingOnly, RoundKind::Ladder})
            for (int n : sizes)
                for (int excluded = 0; excluded < 2; ++excluded)
                {
                    LadderState s = Plain(n, 31);
                    for (Entrant& e : s.entrants) // some spread so the order is not all ties
                    {
                        e.rank = 1 + static_cast<int>(Mix(static_cast<uint32_t>(e.id), 5u) % static_cast<uint32_t>(kBottomRank));
                        e.rating += static_cast<int32_t>(Mix(static_cast<uint32_t>(e.id), 6u) % 5000u);
                    }
                    s.entrants.push_back(Player(1, 3, 3)); // players never play in an NPC round
                    if (n > 4)
                    {
                        Entrant off = Npc(900, 4, 0);
                        off.enabled = false;
                        s.entrants.push_back(off);
                    }
                    RoundOptions o;
                    o.kind = kind;
                    o.excludedNpc = excluded && n > 0 ? NpcId(1) : 0;
                    RoundPlan const p = PlanRound(s, o);
                    int const pool = n - (o.excludedNpc != 0 ? 1 : 0);
                    CHECK(p.round == 1 && p.kind == kind && p.seed == RoundSeed(31, 1) && p.excluded == o.excludedNpc);
                    CHECK(static_cast<int>(p.games.size()) == pool / 2);
                    CHECK((p.bye != 0) == (pool % 2 == 1));
                    std::set<EntrantId> seen;
                    bool slotsOk = true, noSelf = true, seedsOk = true;
                    for (std::size_t i = 0; i < p.games.size(); ++i)
                    {
                        GamePlan const& g = p.games[i];
                        slotsOk = slotsOk && g.slot == i && g.firstSide == -1;
                        noSelf = noSelf && g.a != g.b;
                        seedsOk = seedsOk && g.seed == GameSeed(p.seed, g.slot);
                        seen.insert(g.a);
                        seen.insert(g.b);
                    }
                    if (p.bye != 0)
                        seen.insert(p.bye);
                    CHECK(slotsOk && noSelf && seedsOk);
                    CHECK(static_cast<int>(seen.size()) == pool); // every enabled NPC exactly once (or bye / excluded)
                    CHECK(seen.count(PlayerId(1)) == 0 && seen.count(NpcId(900)) == 0 && seen.count(o.excludedNpc) == 0);
                    CHECK(p.bye != o.excludedNpc || p.bye == 0);
                }

        // The same state gives the same plan; another ladder seed another one.
        LadderState s = Plain(40, 5);
        CHECK(PlanKey(PlanRound(s)) == PlanKey(PlanRound(s)));
        RoundOptions rating;
        rating.kind = RoundKind::RatingOnly;
        std::string const key = PlanKey(PlanRound(s, rating));
        CHECK(key == PlanKey(PlanRound(s, rating)));
        LadderState t = s;
        t.seed = 6;
        CHECK(PlanKey(PlanRound(t, rating)) != key);

        // Round 1 of a start: all ratings equal, so the pairing is a seeded shuffle (not the id order).
        int idOrder = 0;
        for (GamePlan const& g : PlanRound(s, rating).games)
            idOrder += g.b == g.a + 1 && g.a % 2 == 1 ? 1 : 0;
        CHECK(idOrder < 8);

        // Later start rounds pair neighbours by hidden rating.
        for (Entrant& e : s.entrants)
            e.rating = 100000 + static_cast<int32_t>((e.id * 37) % 41) * 100 + static_cast<int32_t>(e.id);
        RoundPlan const byRating = PlanRound(s, rating);
        std::vector<Entrant const*> sorted;
        for (Entrant const& e : s.entrants)
            sorted.push_back(&e);
        std::sort(sorted.begin(), sorted.end(), [](Entrant const* a, Entrant const* b) { return a->rating > b->rating; });
        bool neighbours = byRating.games.size() == 20;
        for (std::size_t i = 0; neighbours && i < byRating.games.size(); ++i)
            neighbours = byRating.games[i].a == sorted[2 * i]->id && byRating.games[i].b == sorted[2 * i + 1]->id;
        CHECK(neighbours);

        // Ladder rounds pair neighbours in ladder order.
        for (Entrant& e : s.entrants)
        {
            e.rank = 1 + static_cast<int>(e.id % kBottomRank);
            e.stars = static_cast<int>(e.id % 3);
        }
        RoundPlan const byLadder = PlanRound(s);
        std::vector<Entrant const*> order = LadderOrder(s);
        bool ladderNeighbours = byLadder.games.size() == 20;
        for (std::size_t i = 0; ladderNeighbours && i < byLadder.games.size(); ++i)
            ladderNeighbours = byLadder.games[i].a == order[2 * i]->id && byLadder.games[i].b == order[2 * i + 1]->id;
        CHECK(ladderNeighbours);
    }

    void TestNoRepeat()
    {
        // Three neighbour pairs that all just played each other: the plan reshuffles them.
        LadderState s = Plain(6, 9);
        for (uint32_t i = 1; i <= 6; ++i)
        {
            s.Find(NpcId(i))->rank = 5 + static_cast<int>(i); // order 1..6
            s.Find(NpcId(i))->lastOpponent = NpcId(i % 2 == 1 ? i + 1 : i - 1);
        }
        RoundPlan p = PlanRound(s);
        bool repeat = false;
        for (GamePlan const& g : p.games)
            repeat = repeat || s.Find(g.a)->lastOpponent == g.b;
        CHECK(p.games.size() == 3 && !repeat);

        // Only the last pair repeats: it is fixed with an earlier pair.
        s = Plain(6, 9);
        for (uint32_t i = 1; i <= 6; ++i)
            s.Find(NpcId(i))->rank = 5 + static_cast<int>(i);
        s.Find(NpcId(5))->lastOpponent = NpcId(6);
        s.Find(NpcId(6))->lastOpponent = NpcId(5);
        p = PlanRound(s);
        repeat = false;
        for (GamePlan const& g : p.games)
            repeat = repeat || (g.a == NpcId(5) && g.b == NpcId(6));
        CHECK(p.games.size() == 3 && !repeat);

        // Two NPCs only: the repeat cannot be avoided and happens.
        LadderState two = Plain(2, 9);
        two.entrants[0].lastOpponent = NpcId(2);
        two.entrants[1].lastOpponent = NpcId(1);
        CHECK(PlanRound(two).games.size() == 1);

        // Over many rounds with real results and a bye, an avoidable repeat never happens.
        for (int n : {8, 9, 12, 25})
        {
            LadderState l = Plain(n, 123);
            int repeats = 0;
            for (int round = 0; round < 300; ++round)
            {
                RoundPlan const plan = PlanRound(l);
                for (GamePlan const& g : plan.games)
                    repeats += l.Find(g.a)->lastOpponent == g.b ? 1 : 0;
                RoundReport const r = ApplyRound(l, plan, PlayRound(plan, Fake()));
                CHECK(r.ok);
            }
            CHECK(repeats == 0);
        }
    }

    void TestRotatingBye()
    {
        LadderState s = Plain(7, 17);
        for (int round = 0; round < 70; ++round)
            CHECK(RunRound(s, Fake()).ok);
        uint32_t mn = 1000, mx = 0, sum = 0;
        for (Entrant const& e : s.entrants)
        {
            mn = std::min(mn, e.byes);
            mx = std::max(mx, e.byes);
            sum += e.byes;
            CHECK(e.games + e.byes == 70);
        }
        CHECK(sum == 70 && mn == 10 && mx == 10);

        // An excluded NPC is not a bye and does not disturb the rotation of the rest.
        LadderState x = Plain(8, 17);
        for (int round = 0; round < 70; ++round)
        {
            RoundOptions o;
            o.excludedNpc = NpcId(3);
            CHECK(RunRound(x, Fake(), 1, o).ok);
        }
        mn = 1000;
        mx = 0;
        for (Entrant const& e : x.entrants)
        {
            if (e.id == NpcId(3))
            {
                CHECK(e.byes == 0 && e.games == 0);
                continue;
            }
            mn = std::min(mn, e.byes);
            mx = std::max(mx, e.byes);
        }
        CHECK(mx - mn <= 1 && mn == 10);

        // With an even pool after the exclusion nobody gets a bye.
        LadderState y = Plain(9, 17);
        RoundOptions o;
        o.excludedNpc = NpcId(9);
        RoundReport const r = RunRound(y, Fake(), 1, o);
        uint32_t byes = 0;
        for (Entrant const& e : y.entrants)
            byes += e.byes;
        CHECK(r.ok && byes == 0 && y.Find(NpcId(9))->games == 0);
    }

    // ---- tests: applying a round -------------------------------------------------------------------------

    void TestApplyRound()
    {
        // Ladder round: stars (NPC rules), hidden rating, counters, last opponents, round counter.
        LadderState s = Plain(4, 2);
        s.Find(NpcId(1))->rank = 8;
        s.Find(NpcId(1))->stars = 2;
        s.Find(NpcId(2))->rank = 8;
        s.Find(NpcId(2))->stars = 1;
        s.Find(NpcId(3))->rank = 5;
        s.Find(NpcId(3))->stars = 0;
        s.Find(NpcId(4))->rank = 5;
        s.Find(NpcId(4))->stars = 5;
        RoundPlan const plan = PlanRound(s);
        CHECK(plan.games.size() == 2);
        std::vector<GameResult> res;
        res.push_back(Res(plan.games[0], SimOutcome::Win0));
        res.push_back(Res(plan.games[1], SimOutcome::Draw));
        LadderState before = s;
        RoundReport const r = ApplyRound(s, plan, res);
        CHECK(r.ok && r.played == 2 && r.failures.empty() && r.round == 1 && s.roundsPlayed == 1);
        CHECK(r.touched.size() == 4);
        Entrant const* w = s.Find(plan.games[0].a);
        Entrant const* l = s.Find(plan.games[0].b);
        Stars const wb = before.Find(w->id)->Position();
        Stars const lb = before.Find(l->id)->Position();
        CHECK(w->Position() == StarUp(wb) && l->Position() == StarDown(lb));
        CHECK(w->rating == kStartRating + 1600 && l->rating == kStartRating - 1600);
        CHECK(w->wins == 1 && l->losses == 1 && w->lastOpponent == l->id && l->lastOpponent == w->id);
        Entrant const* d0 = s.Find(plan.games[1].a);
        CHECK(d0->Position() == before.Find(d0->id)->Position() && d0->draws == 1);

        // The same results cannot be applied twice, and results must belong to the plan.
        uint64_t const h = StateHash(s);
        RoundReport const again = ApplyRound(s, plan, res);
        CHECK(!again.ok && again.error == "stale_round" && StateHash(s) == h);
        RoundPlan const next = PlanRound(s);
        RoundReport const wrong = ApplyRound(s, next, std::vector<GameResult>());
        CHECK(!wrong.ok && wrong.error == "results_do_not_match_plan" && StateHash(s) == h);

        // A rating-only round changes hidden ratings and counters but no stars, and starts no Legend check.
        LadderState q = Plain(4, 2);
        for (Entrant& e : q.entrants)
        {
            e.rank = 1;
            e.stars = 5;
        }
        RoundOptions rating;
        rating.kind = RoundKind::RatingOnly;
        CHECK(RunRound(q, AlwaysWin0(), 1, rating).ok);
        for (Entrant const& e : q.entrants)
            CHECK(e.rank == 1 && e.stars == 5 && e.games == 1 && e.rating != kStartRating);
        CHECK(CountRank(q, 0, 0) == 0);

        // A ladder round with the same wins promotes the two winners to Legend.
        RoundReport const lr = RunRound(q, AlwaysWin0());
        CHECK(lr.ok && CountRank(q, 0, 0) == 2 && q.roundsPlayed == 2);
    }

    void TestFailedGames()
    {
        // A failed game changes nothing for either NPC and is reported; the other game counts.
        LadderState s = Plain(5, 8); // five NPCs: two games and a bye
        GameFn flaky = [](GamePlan const& g) {
            if (g.slot == 0)
            {
                SimResult r;
                r.error = "boom";
                return r;
            }
            return FakeGame(g);
        };
        RoundPlan const plan = PlanRound(s);
        CHECK(plan.games.size() == 2 && plan.bye != 0);
        LadderState const before = s;
        RoundReport const r = ApplyRound(s, plan, PlayRound(plan, flaky));
        CHECK(r.ok && r.played == 1 && r.failures.size() == 1 && r.failures[0].plan.slot == 0);
        CHECK(r.failures[0].outcome == SimOutcome::Failed && r.failures[0].error == "boom");
        for (EntrantId id : {plan.games[0].a, plan.games[0].b})
        {
            Entrant const& e = *s.Find(id);
            Entrant const& was = *before.Find(id);
            CHECK(e.rating == was.rating && e.games == 0 && e.rank == was.rank && e.stars == was.stars);
            CHECK(e.lastOpponent == 0 && e.wins == 0 && e.losses == 0 && e.draws == 0);
            CHECK(std::find(r.touched.begin(), r.touched.end(), id) == r.touched.end());
        }
        CHECK(s.roundsPlayed == 1); // the round counts; the failed game is not rerolled
        CHECK(s.Find(plan.bye)->byes == 1);

        // Everything failing changes only the round counter (and the bye).
        LadderState t = Plain(4, 8);
        GameFn broken = [](GamePlan const&) { return SimResult(); };
        RoundReport const all = RunRound(t, broken);
        CHECK(all.ok && all.played == 0 && all.failures.size() == 2 && all.touched.empty() && t.roundsPlayed == 1);
        for (Entrant const& e : t.entrants)
            CHECK(e.games == 0 && e.rating == kStartRating && e.rank == kBottomRank);

        // A game function that throws becomes a failed result, serial and on threads.
        GameFn thrower = [](GamePlan const& g) -> SimResult {
            if (g.slot % 2 == 1)
                throw std::runtime_error("bad alloc");
            return FakeGame(g);
        };
        LadderState u = Plain(10, 8);
        RoundPlan const up = PlanRound(u);
        for (int threads : {1, 3})
        {
            std::vector<GameResult> const out = PlayRound(up, thrower, threads);
            CHECK(out.size() == 5);
            for (std::size_t i = 0; i < out.size(); ++i)
                CHECK((out[i].outcome == SimOutcome::Failed) == (i % 2 == 1));
            CHECK(out[1].error.find("exception") == 0);
        }
    }

    void TestThreadsEqualSerial()
    {
        // The fake games: 101 NPCs, one thread against 2, 4, 16 workers.
        LadderState base = Plain(101, 55);
        RoundPlan const plan = PlanRound(base);
        std::vector<GameResult> const serial = PlayRound(plan, Fake(), 1);
        for (int threads : {2, 4, 16})
        {
            std::vector<GameResult> const par = PlayRound(plan, Fake(), threads);
            bool same = par.size() == serial.size();
            for (std::size_t i = 0; same && i < par.size(); ++i)
                same = par[i].plan.slot == serial[i].plan.slot && par[i].outcome == serial[i].outcome &&
                       par[i].firstSide == serial[i].firstSide && par[i].turns == serial[i].turns;
            CHECK(same);
            LadderState a = base, b = base;
            ApplyRound(a, plan, serial);
            ApplyRound(b, plan, par);
            CHECK(StateHash(a) == StateHash(b));
        }
        // Whole runs agree too.
        LadderState a = Plain(31, 3), b = Plain(31, 3);
        RunLadderStart(a, Fake(), 1);
        RunLadderStart(b, Fake(), 4);
        for (int i = 0; i < 30; ++i)
        {
            RunRound(a, Fake(), 1);
            RunRound(b, Fake(), 4);
        }
        CHECK(StateHash(a) == StateHash(b));
    }

    // Real games: a small roster from the real library and finder names.
    LadderState RealRoster(std::size_t count, uint32_t seed)
    {
        LadderState s;
        s.seed = seed;
        std::vector<FinderName> names(Finder().begin(), Finder().begin() + static_cast<std::ptrdiff_t>(count));
        UpdateRoster(s, names, Library());
        return s;
    }

    void TestRealGames()
    {
        Match::CardDb const& db = MatchAllCards::Db();
        LadderState s = RealRoster(6, 99);
        CHECK(s.entrants.size() == 6);
        SimRoster const roster = BuildSimRoster(s, Library());
        CHECK(roster.size() == 6);
        GameFn const game = MakeSimGameFn(db, roster);

        // One ladder-start round of real games, serial against threads, then the full start and its placement.
        LadderState serial = s, threaded = s;
        RoundOptions rating;
        rating.kind = RoundKind::RatingOnly;
        RoundReport const r1 = RunRound(serial, game, 1, rating);
        RoundReport const r2 = RunRound(threaded, game, 3, rating);
        CHECK(r1.ok && r1.played == 3 && r1.failures.empty());
        CHECK(r2.ok && r2.played == 3 && r2.failures.empty());
        CHECK(StateHash(serial) == StateHash(threaded));
        for (Entrant const& e : serial.entrants)
            CHECK(e.games == 1 && e.rank == kBottomRank);

        LadderRules rules;
        rules.startRounds = 2;
        LadderState straight = s;
        CHECK(RunLadderStart(straight, game, 2, rules) == 2);
        CHECK(straight.started && CountRank(straight, 0, 0) == 6);
        // A crash after the first round: the saved state continues to the same ladder.
        LadderState crashed = s;
        RunRound(crashed, game, 1, rating);
        LadderState resumed = crashed;
        RunLadderStart(resumed, game, 3, rules);
        CHECK(StateHash(resumed) == StateHash(straight));
        CHECK(straight.roundsPlayed == 2);

        // A normal round of real games after the start, serial and threaded alike.
        LadderState a = straight, b = straight;
        RoundReport const ra = RunRound(a, game, 1);
        RoundReport const rb = RunRound(b, game, 3);
        CHECK(ra.ok && ra.failures.empty() && rb.ok && rb.failures.empty() && StateHash(a) == StateHash(b));
        CHECK(a.roundsPlayed == 3);
    }

    void TestFailedRealGame()
    {
        Match::CardDb const& db = MatchAllCards::Db();
        LadderState s = RealRoster(4, 5);
        std::vector<LibraryDeck> lib = Library();
        // Break the deck of the first NPC (an unknown card id) and drop the deck of the last one.
        for (LibraryDeck& d : lib)
            if (d.key == s.entrants[0].deckKey)
                d.cards[0] = "NOT_A_CARD";
        std::string const lostKey = s.entrants[3].deckKey;
        lib.erase(std::remove_if(lib.begin(), lib.end(), [&](LibraryDeck const& d) { return d.key == lostKey; }), lib.end());
        SimRoster const roster = BuildSimRoster(s, lib);
        CHECK(roster.size() == 3 && roster.count(s.entrants[3].id) == 0);
        GameFn const game = MakeSimGameFn(db, roster);

        RoundPlan plan;
        plan.round = 1;
        plan.seed = RoundSeed(s.seed, 1);
        for (uint32_t i = 0; i < 2; ++i)
        {
            GamePlan g;
            g.slot = i;
            g.a = s.entrants[i * 2].id;
            g.b = s.entrants[i * 2 + 1].id;
            g.seed = GameSeed(plan.seed, i);
            plan.games.push_back(g);
        }
        std::vector<GameResult> const results = PlayRound(plan, game, 2);
        CHECK(results[0].outcome == SimOutcome::Failed && results[0].error.find("setup refused") == 0);
        CHECK(results[1].outcome == SimOutcome::Failed && results[1].error.find("no deck") == 0);
        RoundReport const r = ApplyRound(s, plan, results);
        CHECK(r.ok && r.played == 0 && r.failures.size() == 2);
        for (Entrant const& e : s.entrants)
            CHECK(e.games == 0 && e.rating == kStartRating && e.rank == kBottomRank);
        CHECK(s.roundsPlayed == 1);

        // A forced engine refusal through the chooser hook is a failure as well.
        SimOptions illegal;
        illegal.chooser = [](Match::Match const& m, int side, int) {
            Match::Action a;
            if (m.MulliganPending(side))
            {
                a.type = Match::ActionType::Mulligan;
                return a;
            }
            a.type = Match::ActionType::Play;
            a.source = 99999;
            return a;
        };
        LadderState t = RealRoster(2, 5);
        SimRoster const tr = BuildSimRoster(t, Library());
        RoundReport const fr = RunRound(t, MakeSimGameFn(db, tr, illegal), 1);
        CHECK(fr.ok && fr.played == 0 && fr.failures.size() == 1 && fr.failures[0].error.find("refused") != std::string::npos);
        CHECK(t.entrants[0].games == 0 && t.entrants[1].games == 0);
    }

    // ---- tests: crash and replay -------------------------------------------------------------------------

    void TestReplayAfterCrash()
    {
        LadderState live = Plain(30, 4711);
        RunLadderStart(live, Fake(), 2);
        for (int i = 0; i < 12; ++i)
            RunRound(live, Fake(), 2);
        LadderState const saved = live; // the last saved round

        // The round that was lost: planned and played again from the saved state, it is the same round.
        RoundOptions o;
        o.excludedNpc = NpcId(7);
        RoundPlan const lost = PlanRound(live, o);
        std::vector<GameResult> const lostResults = PlayRound(lost, Fake(), 2);
        RoundReport const first = ApplyRound(live, lost, lostResults);

        LadderState restarted = saved;
        RoundPlan const replayPlan = PlanRound(restarted, o);
        CHECK(PlanKey(replayPlan) == PlanKey(lost));
        std::vector<GameResult> const replayResults = PlayRound(replayPlan, Fake(), 1);
        RoundReport const second = ApplyRound(restarted, replayPlan, replayResults);
        CHECK(first.ok && second.ok && first.touched == second.touched && first.pushedOut == second.pushedOut);
        CHECK(StateHash(live) == StateHash(restarted));

        // ... and the ladder goes on identically afterwards.
        for (int i = 0; i < 25; ++i)
        {
            RunRound(live, Fake(), 2);
            RunRound(restarted, Fake(), 1);
        }
        CHECK(StateHash(live) == StateHash(restarted));

        // A player result in between does not break it either.
        LadderState p1 = saved, p2 = saved;
        EnsurePlayer(p1, 1, "Player");
        EnsurePlayer(p2, 1, "Player");
        for (LadderState* st : {&p1, &p2})
        {
            ApplyPlayerResult(*st, PlayerId(1), NpcId(7), Result::Win);
            RoundOptions ex;
            ex.excludedNpc = NpcId(7);
            RunRound(*st, Fake(), st == &p1 ? 1 : 3, ex);
        }
        CHECK(StateHash(p1) == StateHash(p2) && StateHash(p1) != StateHash(saved));
    }

    // ---- tests: the player -------------------------------------------------------------------------------

    void TestPlayerResult()
    {
        LadderState s = Plain(3, 1);
        Entrant& p = EnsurePlayer(s, 7, "Jonas");
        CHECK(p.id == PlayerId(7) && p.rank == kBottomRank && p.stars == 0 && p.rating == kStartRating && p.IsPlayer());
        CHECK(&EnsurePlayer(s, 7, "Jonas") == s.Find(PlayerId(7)) && s.entrants.size() == 4); // idempotent

        // Bad arguments change nothing.
        uint64_t const h = StateHash(s);
        CHECK(ApplyPlayerResult(s, PlayerId(8), NpcId(1), Result::Win).error == "unknown_player");
        CHECK(ApplyPlayerResult(s, NpcId(1), NpcId(2), Result::Win).error == "unknown_player");
        CHECK(ApplyPlayerResult(s, PlayerId(7), NpcId(99), Result::Win).error == "unknown_npc");
        CHECK(ApplyPlayerResult(s, PlayerId(7), PlayerId(7), Result::Win).error == "unknown_npc");
        CHECK(StateHash(s) == h);

        // A win: the player's stars, both ratings (zero-sum), counters, the record against that NPC.
        PlayerResultReport r = ApplyPlayerResult(s, PlayerId(7), NpcId(1), Result::Win);
        CHECK(r.ok && r.before == (Stars{kBottomRank, 0}) && r.after == (Stars{kBottomRank, 1}) && r.newBest == true);
        CHECK(r.ratingDelta == 1600 && s.Find(PlayerId(7))->rating == kStartRating + 1600 && s.Find(NpcId(1))->rating == kStartRating - 1600);
        CHECK(r.npcBefore == (Stars{kBottomRank, 0}) && r.npcAfter == (Stars{kBottomRank, 0})); // the floor
        CHECK(s.Find(PlayerId(7))->wins == 1 && s.Find(NpcId(1))->losses == 1 && s.Find(NpcId(1))->games == 1);
        // The NPC took the win: it climbs one star, no streak bonus.
        r = ApplyPlayerResult(s, PlayerId(7), NpcId(2), Result::Loss);
        CHECK(r.after == (Stars{kBottomRank, 0}) && r.npcAfter == (Stars{kBottomRank, 1}) && r.ratingDelta < 0 && !r.newBest);
        ApplyPlayerResult(s, PlayerId(7), NpcId(1), Result::Win);
        ApplyPlayerResult(s, PlayerId(7), NpcId(1), Result::Draw);
        VsRecord const* v = s.FindVs(PlayerId(7), NpcId(1));
        CHECK(v && v->wins == 2 && v->losses == 0 && v->draws == 1);
        v = s.FindVs(PlayerId(7), NpcId(2));
        CHECK(v && v->wins == 0 && v->losses == 1);
        CHECK(s.FindVs(PlayerId(7), NpcId(3)) == nullptr && s.vs.size() == 2);
        CHECK(s.Find(PlayerId(7))->games == 4 && s.Find(PlayerId(7))->wins == 2 && s.Find(PlayerId(7))->draws == 1);

        // The best position only goes up: a long climb, a fall, and the best stays.
        s = Plain(1, 1);
        EnsurePlayer(s, 1, "P");
        int bestSeen = kBottomRank;
        for (int i = 0; i < 30; ++i)
        {
            ApplyPlayerResult(s, PlayerId(1), NpcId(1), Result::Win);
            bestSeen = std::min(bestSeen, s.Find(PlayerId(1))->rank);
        }
        Entrant const& pl = *s.Find(PlayerId(1));
        CHECK(pl.bestRank == bestSeen && pl.bestRank < kBottomRank);
        int const bestRank = pl.bestRank, bestStars = pl.bestStars;
        for (int i = 0; i < 150; ++i)
            ApplyPlayerResult(s, PlayerId(1), NpcId(1), Result::Loss);
        CHECK(pl.rank == kBottomRank && pl.stars == 0);
        CHECK(pl.bestRank == bestRank && pl.bestStars == bestStars);

        // A disabled NPC can still be played (the result must not be lost), and several player entries coexist.
        s.Find(NpcId(1))->enabled = false;
        EnsurePlayer(s, 2, "Second");
        CHECK(ApplyPlayerResult(s, PlayerId(2), NpcId(1), Result::Win).ok);
        CHECK(s.FindVs(PlayerId(2), NpcId(1))->wins == 1 && s.FindVs(PlayerId(1), NpcId(1))->wins >= 1);
        std::vector<Entrant const*> const order = LadderOrder(s);
        CHECK(order.size() == 2 && order[0]->id == PlayerId(2)); // disabled NPC not listed; player 2 ahead of player 1 (1 star more)

        // Rank 1 full stars + win = Legend, and Legend counts as the best position.
        LadderState l = Plain(1, 1);
        EnsurePlayer(l, 1, "P");
        l.Find(PlayerId(1))->rank = 1;
        l.Find(PlayerId(1))->stars = 5;
        r = ApplyPlayerResult(l, PlayerId(1), NpcId(1), Result::Win);
        CHECK(r.after.rank == kLegend && r.newBest && l.Find(PlayerId(1))->bestRank == kLegend && r.pushedOut.empty());
        r = ApplyPlayerResult(l, PlayerId(1), NpcId(1), Result::Loss);
        CHECK(r.after.rank == kLegend && l.Find(PlayerId(1))->bestRank == kLegend && !r.newBest);
    }

    // ---- tests: the long run -----------------------------------------------------------------------------

    void TestDrift()
    {
        int const n = 30;
        LadderState s = Plain(n, 20261014);
        RunLadderStart(s, Fake(), 2);
        EnsurePlayer(s, 1, "Player");
        int64_t const total = static_cast<int64_t>(n + 1) * kStartRating;
        int maxLegend = 0;
        bool legendOk = true, zeroSum = true;
        for (int round = 0; round < 1000; ++round)
        {
            // The player meets some NPC every round and wins about half of the games; the NPC sits out the round.
            std::vector<Entrant const*> order = LadderOrder(s);
            Entrant const* opponent = nullptr;
            for (std::size_t i = 0; i < order.size(); ++i)
            {
                Entrant const* e = order[(static_cast<std::size_t>(round) * 7 + i) % order.size()];
                if (!e->IsPlayer())
                {
                    opponent = e;
                    break;
                }
            }
            EntrantId const oppId = opponent->id;
            Result const res = Mix(static_cast<uint32_t>(round), 31u) % 100u < 50u ? Result::Win : Result::Loss;
            ApplyPlayerResult(s, PlayerId(1), oppId, res);
            int legend = 0;
            for (Entrant const& e : s.entrants)
                legend += e.rank == kLegend ? 1 : 0;
            maxLegend = std::max(maxLegend, legend);
            legendOk = legendOk && legend <= 10;
            RoundOptions o;
            o.excludedNpc = oppId;
            RunRound(s, Fake(), 1, o);
            legend = 0;
            int64_t sum = 0;
            for (Entrant const& e : s.entrants)
            {
                legend += e.rank == kLegend ? 1 : 0;
                sum += e.rating;
            }
            maxLegend = std::max(maxLegend, legend);
            legendOk = legendOk && legend <= 10;
            zeroSum = zeroSum && sum == total;
        }
        CHECK(legendOk && zeroSum);
        int perBand[5] = {};
        for (Entrant const& e : s.entrants)
        {
            if (e.IsPlayer())
                continue;
            ++perBand[e.rank == kLegend ? 0 : e.rank <= 5 ? 1 : e.rank <= 10 ? 2 : e.rank <= 14 ? 3 : 4];
        }
        std::printf("    after 1000 rounds: Legend %d, rank 1-5 %d, 6-10 %d, 11-14 %d, 15 %d (max Legend %d)\n",
                    perBand[0], perBand[1], perBand[2], perBand[3], perBand[4], maxLegend);
        // Spread out: not everybody at the top (the streak bonus is player only) and not everybody at the bottom.
        CHECK(perBand[1] <= (n - perBand[0]) * 6 / 10);
        CHECK(perBand[2] + perBand[3] + perBand[4] >= 5);
        CHECK(perBand[4] < n / 2);
        // The Legend places went to strong NPCs.
        double legendStrength = 0;
        int legendCount = 0;
        double all = 0;
        for (Entrant const& e : s.entrants)
        {
            if (e.IsPlayer())
                continue;
            all += Strength(e.id) / n;
            if (e.rank == kLegend)
            {
                legendStrength += Strength(e.id);
                ++legendCount;
            }
        }
        CHECK(legendCount > 0 && legendStrength / std::max(1, legendCount) > all + 100.0);
    }

    // ---- tests: the row encodings ------------------------------------------------------------------------

    // The escape of a database connection, as far as the statements need it: the quote and the backslash.
    std::string EscapeSql(std::string const& s)
    {
        std::string out;
        for (char c : s)
        {
            if (c == '\'' || c == '\\')
                out.push_back('\\');
            out.push_back(c);
        }
        return out;
    }

    // The values of "(1, 'a', 2), (3, 'b', 4)" from `pos` on, as cell texts.
    std::vector<std::vector<std::string>> ParseTuples(std::string const& sql, std::size_t pos)
    {
        std::vector<std::vector<std::string>> rows;
        std::vector<std::string> row;
        bool inRow = false;
        while (pos < sql.size())
        {
            char const c = sql[pos];
            if (c == '(' && !inRow)
            {
                inRow = true;
                row.clear();
                ++pos;
            }
            else if (inRow && (c == '\'' || c == '"'))
            {
                std::string value;
                ++pos;
                while (pos < sql.size() && sql[pos] != c)
                {
                    if (sql[pos] == '\\')
                        ++pos;
                    value.push_back(sql[pos++]);
                }
                ++pos;
                row.push_back(value);
            }
            else if (inRow && (std::isdigit(static_cast<unsigned char>(c)) || c == '-'))
            {
                std::size_t const end = sql.find_first_of(",)", pos);
                row.push_back(sql.substr(pos, end - pos));
                pos = end;
            }
            else if (inRow && c == ')')
            {
                rows.push_back(row);
                inRow = false;
                ++pos;
            }
            else if (inRow || c == ',' || c == ' ' || c == '\n' || c == '\r')
                ++pos;
            else
                break;
        }
        return rows;
    }

    // The character database as the statements of the ladder store leave it: the cells of every row by table and key.
    struct MockDb
    {
        std::map<std::string, std::map<std::string, std::vector<std::string>>> tables;
        int statements = 0;

        static std::size_t KeyCells(std::string const& table)
        {
            return table == kTableVs || table == kTableGame ? 2 : 1;
        }

        bool Run(std::string const& sql)
        {
            ++statements;
            std::size_t const a = sql.find('`'), b = sql.find('`', a + 1);
            if (a == std::string::npos || b == std::string::npos)
                return false;
            std::string const table = sql.substr(a + 1, b - a - 1);
            if (sql.compare(0, 12, "DELETE FROM ") == 0)
            {
                tables[table].clear();
                return true;
            }
            bool const replace = sql.compare(0, 13, "REPLACE INTO ") == 0;
            bool const upsert = sql.compare(0, 12, "INSERT INTO ") == 0 && table == kTableLadder;
            if (!replace && !upsert)
                return false;
            std::size_t const values = sql.find(" VALUES ");
            if (values == std::string::npos)
                return false;
            // The column list in the statement is the one the readers expect.
            char const* columns = table == kTableLadder ? kLadderColumns : table == kTableEntrant ? kEntrantColumns
                : table == kTableVs ? kVsColumns : kGameColumns;
            if (sql.find(std::string("(") + columns + ") VALUES ") == std::string::npos)
                return false;
            for (std::vector<std::string> const& cells : ParseTuples(sql, values + 8))
            {
                std::string key;
                for (std::size_t i = 0; i < KeyCells(table); ++i)
                    key += cells[i] + "|";
                tables[table][key] = cells;
            }
            return true;
        }

        bool Run(std::vector<std::string> const& statementList)
        {
            for (std::string const& sql : statementList)
                if (!Run(sql))
                    return false;
            return true;
        }

        // What the store's Load does: the cells through the readers, then BuildState.
        bool Load(LadderState& out)
        {
            auto const& ladderRows = tables[kTableLadder];
            LadderRow ladder;
            if (ladderRows.size() != 1 || !LadderRowFromCells(ladderRows.begin()->second, ladder))
                return false;
            std::vector<EntrantRow> entrants;
            for (auto const& kv : tables[kTableEntrant])
            {
                EntrantRow row;
                if (!EntrantRowFromCells(kv.second, row))
                    return false;
                entrants.push_back(row);
            }
            std::vector<VsRow> vs;
            for (auto const& kv : tables[kTableVs])
            {
                VsRow row;
                if (!VsRowFromCells(kv.second, row))
                    return false;
                vs.push_back(row);
            }
            std::string error;
            return BuildState(ladder, entrants, vs, out, error);
        }
    };

    constexpr uint32_t kRules = 11;

    // The fake game, failing for some games.
    GameFn FakeWithFailures()
    {
        return [](GamePlan const& g) {
            if (Mix(g.seed, 77u) % 9u == 0)
            {
                SimResult r;
                r.error = "boom 'quoted' \\ slash";
                return r;
            }
            return FakeGame(g);
        };
    }

    void CheckLoaded(MockDb& db, LadderState const& live)
    {
        LadderState loaded;
        bool const ok = db.Load(loaded);
        CHECK(ok);
        if (ok)
        {
            CHECK(StateHash(loaded) == StateHash(live));
            CHECK(loaded.entrants.size() == live.entrants.size() && loaded.vs.size() == live.vs.size());
            CHECK(loaded.seed == live.seed && loaded.roundsPlayed == live.roundsPlayed && loaded.started == live.started);
        }
    }

    // Every save of the store, in the order of a real ladder, against the mock database: after each one the loaded
    // state has the hash of the live state.
    void TestRowsSaveAndLoad()
    {
        MockDb db;
        LadderState live = RealRoster(24, 20261014);
        live.entrants[0].name = "O'Brien \\ the \"Brave\"";
        live.entrants[1].name = std::string(300, 'x') + "\xC3\xA9"; // cut to the column size
        CHECK(live.entrants.size() == 24);

        // A new ladder: one roster save with everybody.
        std::vector<EntrantId> all;
        for (Entrant const& e : live.entrants)
            all.push_back(e.id);
        CHECK(db.Run(RosterSaveSql(live, all, kRules, EscapeSql)));
        LadderState loaded;
        CHECK(db.Load(loaded));
        CHECK(StateHash(loaded) == StateHash(live));
        CHECK(loaded.Find(live.entrants[0].id)->name == live.entrants[0].name);
        CHECK(loaded.Find(live.entrants[1].id)->name.size() <= kNameBytes && loaded.Find(live.entrants[1].id)->name.size() > 90);
        for (Entrant const& e : live.entrants)
            CHECK(loaded.Find(e.id)->deckKey == e.deckKey && loaded.Find(e.id)->heroClass == e.heroClass &&
                  loaded.Find(e.id)->profile == e.profile && loaded.Find(e.id)->faction == e.faction);
        LadderState const fresh = live;

        // The ladder start: each round saved on its own (with failed games in it), then the placement.
        LadderRules rules;
        rules.startRounds = 6;
        uint32_t games = 0, failedGames = 0;
        while (live.roundsPlayed < static_cast<uint32_t>(rules.startRounds))
        {
            RoundOptions o;
            o.kind = RoundKind::RatingOnly;
            RoundPlan const plan = PlanRound(live, o);
            std::vector<GameResult> const results = PlayRound(plan, FakeWithFailures(), 2);
            RoundReport const report = ApplyRound(live, plan, results, rules);
            CHECK(report.ok);
            CHECK(db.Run(RoundSaveSql(live, plan, results, report, kRules, EscapeSql)));
            games += static_cast<uint32_t>(results.size());
            for (GameResult const& g : results)
                failedGames += g.outcome == SimOutcome::Failed ? 1 : 0;
            CheckLoaded(db, live);
        }
        CHECK(failedGames > 0 && games == db.tables[kTableGame].size());
        PlaceNpcs(live, rules);
        CHECK(db.Run(PlacementSaveSql(live, kRules, EscapeSql)));
        CheckLoaded(db, live);

        // The game rows: the plan, the outcome code, the error text.
        uint32_t seenFailed = 0;
        for (auto const& kv : db.tables[kTableGame])
        {
            GameRow row;
            CHECK(GameRowFromCells(kv.second, row));
            CHECK(row.ratingOnly && row.round >= 1 && row.round <= 6 && row.a != row.b);
            CHECK(row.outcome == "win0" || row.outcome == "win1" || row.outcome == "draw" || row.outcome == "failed");
            if (row.outcome == "failed")
            {
                ++seenFailed;
                CHECK(row.error == "boom 'quoted' \\ slash" && row.firstSide == -1);
            }
            else
                CHECK(row.error.empty() && row.turns == 10 && (row.firstSide == 0 || row.firstSide == 1));
        }
        CHECK(seenFailed == failedGames);

        // Normal life: player results (then the NPC round without that NPC), rounds, a name leaving and coming
        // back, a new name joining. After every save the database equals the live state.
        std::vector<FinderName> finder(Finder().begin(), Finder().begin() + 24);
        std::vector<FinderName> const fullList = finder;
        std::size_t nextNew = 24;
        uint32_t legendPushed = 0;
        for (int step = 0; step < 90; ++step)
        {
            if (step % 15 == 7)
            {
                // A roster change: drop one name, later bring it back and add a new one.
                if (finder.size() == fullList.size())
                    finder.erase(finder.begin() + 3);
                else
                {
                    finder = fullList;
                    finder.push_back(Finder()[nextNew++]);
                }
                RosterChange const c = UpdateRoster(live, finder, Library(), rules);
                std::vector<EntrantId> changed = c.added;
                changed.insert(changed.end(), c.enabled.begin(), c.enabled.end());
                changed.insert(changed.end(), c.disabled.begin(), c.disabled.end());
                changed.insert(changed.end(), c.pushedOut.begin(), c.pushedOut.end());
                CHECK(!changed.empty());
                CHECK(db.Run(RosterSaveSql(live, changed, kRules, EscapeSql)));
                CheckLoaded(db, live);
                continue;
            }
            std::vector<Entrant const*> const order = LadderOrder(live);
            Entrant const* opponent = nullptr;
            for (std::size_t i = 0; i < order.size() && !opponent; ++i)
                if (!order[(static_cast<std::size_t>(step) * 5 + i) % order.size()]->IsPlayer())
                    opponent = order[(static_cast<std::size_t>(step) * 5 + i) % order.size()];
            EntrantId const npc = opponent->id;
            uint32_t const account = 1 + static_cast<uint32_t>(step % 3 == 0);
            EntrantId const playerId = EnsurePlayer(live, account, "Jonas'" + std::to_string(account)).id;
            Result const result = step % 11 == 0 ? Result::Draw : Mix(static_cast<uint32_t>(step), 5u) % 3u != 0 ? Result::Win : Result::Loss;
            PlayerResultReport const r = ApplyPlayerResult(live, playerId, npc, result, rules);
            CHECK(r.ok);
            legendPushed += static_cast<uint32_t>(r.pushedOut.size());
            CHECK(db.Run(PlayerResultSaveSql(live, playerId, npc, r.pushedOut, kRules, EscapeSql)));
            CheckLoaded(db, live);

            RoundOptions o;
            o.excludedNpc = npc;
            RoundPlan const plan = PlanRound(live, o);
            std::vector<GameResult> const results = PlayRound(plan, FakeWithFailures(), 1);
            RoundReport const report = ApplyRound(live, plan, results, rules);
            CHECK(report.ok);
            legendPushed += static_cast<uint32_t>(report.pushedOut.size());
            CHECK(db.Run(RoundSaveSql(live, plan, results, report, kRules, EscapeSql)));
            CheckLoaded(db, live);
        }
        CHECK(live.roundsPlayed > 60 && live.Find(PlayerId(1)) && live.Find(PlayerId(2)) && !live.vs.empty());
        bool anyDisabled = false;
        for (Entrant const& e : live.entrants)
            anyDisabled = anyDisabled || (!e.IsPlayer() && !e.enabled);
        std::printf("    %d statements, %u pushed out of Legend, a disabled NPC left: %s\n", db.statements, legendPushed,
                    anyDisabled ? "yes" : "no");

        // A reset empties everything; a new ladder can be saved after it.
        CHECK(db.Run(ResetSql()));
        for (char const* t : {kTableLadder, kTableEntrant, kTableVs, kTableGame})
            CHECK(db.tables[t].empty());
        LadderState none;
        CHECK(!db.Load(none));
        CHECK(db.Run(RosterSaveSql(fresh, all, kRules, EscapeSql)));
        CheckLoaded(db, fresh);
    }

    std::vector<std::string> SplitColumns(std::string const& list)
    {
        std::vector<std::string> out;
        std::stringstream in(list);
        std::string part;
        while (std::getline(in, part, ','))
        {
            part.erase(std::remove_if(part.begin(), part.end(), [](unsigned char c) { return c == '`' || c == ' '; }), part.end());
            out.push_back(part);
        }
        return out;
    }

    // The statements and the readers against the migration file: the same columns in the same order, and the text
    // limits of the VARCHAR columns.
    void TestRowsMigration()
    {
        std::string sql;
        CHECK(ReadFile(g_dir + "/../data/sql/character/0015_hearthstone_ladder.sql", sql));
        struct Table
        {
            char const* name;
            char const* columns;
            std::size_t count;
        };
        Table const tables[] = {
            {kTableLadder, kLadderColumns, kLadderColumnCount},
            {kTableEntrant, kEntrantColumns, kEntrantColumnCount},
            {kTableVs, kVsColumns, kVsColumnCount},
            {kTableGame, kGameColumns, kGameColumnCount},
        };
        for (Table const& t : tables)
        {
            std::size_t const pos = sql.find(std::string("CREATE TABLE IF NOT EXISTS `") + t.name + "` (");
            CHECK(pos != std::string::npos);
            if (pos == std::string::npos)
                continue;
            std::size_t const end = sql.find(") ENGINE=", pos);
            std::istringstream in(sql.substr(pos, end - pos));
            std::string line;
            std::vector<std::string> names;
            while (std::getline(in, line))
            {
                if (line.size() < 4 || line.compare(0, 3, "  `") != 0)
                    continue;
                names.push_back(line.substr(3, line.find('`', 3) - 3));
            }
            std::vector<std::string> const wanted = SplitColumns(t.columns);
            CHECK(wanted.size() == t.count);
            // The ladder row also has two time stamps, which the statements leave to their defaults.
            if (std::string(t.name) == kTableLadder)
            {
                CHECK(names.size() == wanted.size() + 2 && names[names.size() - 2] == "created_at" && names.back() == "updated_at");
                names.resize(wanted.size());
            }
            CHECK(names == wanted);
        }

        // The VARCHAR limits are the ones the writers cut to.
        auto varchar = [&](char const* table, char const* column) -> std::size_t {
            std::size_t pos = sql.find(std::string("CREATE TABLE IF NOT EXISTS `") + table + "`");
            pos = sql.find(std::string("  `") + column + "` VARCHAR(", pos);
            return pos == std::string::npos ? 0 : static_cast<std::size_t>(std::atoi(sql.c_str() + pos + std::strlen(column) + 13));
        };
        CHECK(varchar(kTableEntrant, "name") == kNameBytes);
        CHECK(varchar(kTableEntrant, "hero_class") == kClassBytes);
        CHECK(varchar(kTableEntrant, "deck_key") == kDeckKeyBytes);
        CHECK(varchar(kTableGame, "error") == kErrorBytes);
        CHECK(varchar(kTableGame, "outcome") >= 6);
        // What the roster hands out fits: library deck keys and classes.
        CHECK(!Library().empty());
        for (LibraryDeck const& d : Library())
            CHECK(d.key.size() <= kDeckKeyBytes && d.heroClass.size() <= kClassBytes);
        for (char const* code : {"alliance", "horde", "any", "normal", "hard"})
            CHECK(sql.find(std::string("'") + code + "'") != std::string::npos);
        // The migration is additive and safe to run twice.
        CHECK(sql.find("DROP ") == std::string::npos && sql.find("ALTER ") == std::string::npos);
        std::size_t creates = 0, guarded = 0;
        for (std::size_t p = sql.find("\nCREATE TABLE"); p != std::string::npos; p = sql.find("\nCREATE TABLE", p + 1))
            ++creates;
        for (std::size_t p = sql.find("\nCREATE TABLE IF NOT EXISTS"); p != std::string::npos;
             p = sql.find("\nCREATE TABLE IF NOT EXISTS", p + 1))
            ++guarded;
        CHECK(creates == 4 && guarded == 4);
    }

    void TestRowsMalformed()
    {
        Entrant good = Npc(5, 7, 3, -250);
        good.faction = Faction::Horde;
        good.heroClass = "MAGE";
        good.deckKey = "deck_x";
        good.profile = Match::AiProfile::Hard;
        good.winStreak = 4;
        good.lastOpponent = PlayerId(9);
        EntrantRow const row = ToRow(good);
        Entrant back;
        std::string error;
        CHECK(FromRow(row, back, error));
        LadderState a, b;
        a.entrants.push_back(good);
        b.entrants.push_back(back);
        CHECK(StateHash(a) == StateHash(b) && back.name == good.name && back.rating == -250);

        // Rows that are not on the ladder are refused, with a reason.
        EntrantRow bad = row;
        bad.faction = "neutral";
        CHECK(!FromRow(bad, back, error) && error.find("faction") != std::string::npos);
        bad = row;
        bad.profile = "easy";
        CHECK(!FromRow(bad, back, error) && error.find("profile") != std::string::npos);
        bad = row;
        bad.rank = kBottomRank + 1;
        CHECK(!FromRow(bad, back, error));
        bad = row;
        bad.stars = 6; // rank 7 holds 5
        CHECK(!FromRow(bad, back, error));
        bad = row;
        bad.rank = 0;
        bad.stars = 1; // Legend has no stars
        CHECK(!FromRow(bad, back, error));
        bad = row;
        bad.bestRank = kBottomRank;
        bad.bestStars = 3; // the bottom rank holds 2
        CHECK(!FromRow(bad, back, error));

        LadderRow const ladder = ToLadderRow(a, kRules);
        std::vector<EntrantRow> const twice = {row, row};
        LadderState out;
        CHECK(!BuildState(ladder, twice, {}, out, error) && error.find("twice") != std::string::npos);
        CHECK(BuildState(ladder, {row}, {}, out, error) && StateHash(out) == StateHash(a));

        // Cells that are not numbers, too few or too many.
        std::vector<std::string> const cells = {"5", "n", "any", "MAGE", "d", "normal", "1", "0", "15", "0", "100000",
                                                "0", "0", "0", "0", "0", "15", "0", "0", "0"};
        EntrantRow r;
        CHECK(EntrantRowFromCells(cells, r) && r.id == 5 && r.rating == 100000 && r.enabled);
        std::vector<std::string> broken = cells;
        broken[10] = "10.5";
        CHECK(!EntrantRowFromCells(broken, r));
        broken = cells;
        broken[8] = "-1";
        CHECK(!EntrantRowFromCells(broken, r));
        broken = cells;
        broken[0] = "";
        CHECK(!EntrantRowFromCells(broken, r));
        broken = cells;
        broken[6] = "2";
        CHECK(!EntrantRowFromCells(broken, r));
        broken = cells;
        broken.push_back("0");
        CHECK(!EntrantRowFromCells(broken, r));
        broken = cells;
        broken.pop_back();
        CHECK(!EntrantRowFromCells(broken, r));
        broken = cells;
        broken[10] = "-2147483648";
        CHECK(EntrantRowFromCells(broken, r) && r.rating == INT32_MIN);
        broken[10] = "-2147483649";
        CHECK(!EntrantRowFromCells(broken, r));
        broken[10] = "99999999999999999999999";
        CHECK(!EntrantRowFromCells(broken, r));
        broken = cells;
        broken[19] = "18446744073709551615";
        CHECK(EntrantRowFromCells(broken, r) && r.lastOpponent == UINT64_MAX);

        LadderRow lr;
        CHECK(LadderRowFromCells({"1", "7", "20", "1", "11"}, lr) && lr.seed == 7 && lr.roundsPlayed == 20 && lr.started && lr.rulesVersion == 11);
        CHECK(!LadderRowFromCells({"2", "7", "20", "1", "11"}, lr)); // the ladder row is always id 1
        CHECK(!LadderRowFromCells({"1", "7", "20", "2", "11"}, lr));
        CHECK(!LadderRowFromCells({"1", "7", "20", "1"}, lr));
        VsRow vr;
        CHECK(VsRowFromCells({"4294967297", "5", "3", "2", "1"}, vr) && vr.player == PlayerId(1) && vr.npc == 5 && vr.losses == 2);
        CHECK(!VsRowFromCells({"1", "5", "3", "x", "1"}, vr) && !VsRowFromCells({"1", "5", "3", "2"}, vr));
        GameRow gr;
        CHECK(GameRowFromCells({"3", "0", "1", "5", "6", "99", "win0", "-1", "12", ""}, gr) && gr.firstSide == -1 && gr.ratingOnly && gr.outcome == "win0");
        CHECK(!GameRowFromCells({"3", "0", "1", "5", "6", "99", "win0", "2", "12", ""}, gr));
        CHECK(!GameRowFromCells({"3", "0", "2", "5", "6", "99", "win0", "0", "12", ""}, gr));

        // Nothing to write is nothing to run.
        CHECK(EntrantReplaceSql({}, EscapeSql).empty() && VsReplaceSql({}).empty() && GameReplaceSql({}, EscapeSql).empty());
        // Long inserts are split in chunks, every row once.
        std::vector<EntrantRow> many;
        for (uint32_t i = 1; i <= 250; ++i)
        {
            EntrantRow m = row;
            m.id = i;
            many.push_back(m);
        }
        std::vector<std::string> const chunks = EntrantReplaceSql(many, EscapeSql);
        CHECK(chunks.size() == 3);
        std::size_t rowsInChunks = 0;
        for (std::string const& c : chunks)
            rowsInChunks += ParseTuples(c, c.find(" VALUES ") + 8).size();
        CHECK(rowsInChunks == 250);

        // The texts of the GM commands.
        CHECK(RatingText(100000) == "1000.00" && RatingText(-50) == "-0.50" && RatingText(5) == "0.05" && RatingText(0) == "0.00");
        CHECK(RankText(0, 0) == "Legend" && RankText(7, 1) == "Rank 7, 1 star" && RankText(15, 0) == "Rank 15, 0 stars");
        CHECK(ClassText("WARLOCK") == "Warlock" && ClassText("").empty());
    }

    // ---- tests: the offers of the ladder chair -------------------------------------------------------

    // n NPCs 1..n spread over ranks 1..15, classes and factions in rotation (alliance, horde, any), distinct ratings.
    LadderState OfferLadder(int n, uint32_t seed = 11)
    {
        LadderState s;
        s.seed = seed;
        for (int i = 1; i <= n; ++i)
        {
            Entrant e = Npc(static_cast<uint32_t>(i), 1 + (n - i) * 14 / std::max(1, n - 1), i % 3, 100000 - i * 10);
            e.heroClass = kClasses[i % 9];
            e.faction = i % 3 == 0 ? Faction::Any : i % 3 == 1 ? Faction::Alliance : Faction::Horde;
            s.entrants.push_back(e);
        }
        return s;
    }

    // The enabled NPCs in ladder order, and the place a player entry takes between them.
    std::vector<EntrantId> NpcOrderOf(LadderState const& s)
    {
        std::vector<EntrantId> ids;
        for (Entrant const* e : LadderOrder(s))
            if (!e->IsPlayer())
                ids.push_back(e->id);
        return ids;
    }

    std::size_t PlaceOf(LadderState const& s, Entrant const& player)
    {
        std::size_t p = 0;
        for (Entrant const* e : LadderOrder(s))
            if (!e->IsPlayer() && LadderLess(*e, player))
                ++p;
        return p;
    }

    std::size_t IndexOf(std::vector<EntrantId> const& order, EntrantId id)
    {
        return static_cast<std::size_t>(std::find(order.begin(), order.end(), id) - order.begin());
    }

    int FactionMask(LadderState const& s, std::vector<EntrantId> const& ids)
    {
        int mask = 0;
        for (EntrantId id : ids)
            mask |= s.Find(id)->faction == Faction::Alliance ? 1 : s.Find(id)->faction == Faction::Horde ? 2 : 0;
        return mask;
    }

    std::size_t ClassCount(LadderState const& s, std::vector<EntrantId> const& ids)
    {
        std::set<std::string> classes;
        for (EntrantId id : ids)
            classes.insert(s.Find(id)->heroClass);
        return classes.size();
    }

    void TestOffersDraw()
    {
        std::set<EntrantId> const none;
        LadderState s = OfferLadder(60);
        s.entrants.push_back(Player(1, 8, 2, 100000));
        Entrant const& player = *s.Find(PlayerId(1));
        std::vector<EntrantId> const order = NpcOrderOf(s);
        CHECK(order.size() == 60);
        std::size_t const place = PlaceOf(s, player);
        CHECK(place > 10 && place < 50);

        // The basics, over many seeds: three different enabled NPCs from the window of 5 above and 5 below, in ladder
        // order, different classes, both factions; the same seed gives the same offers.
        int bothFactions = 0, threeClasses = 0, differs = 0;
        int closePicks = 0, farPicks = 0;
        std::vector<EntrantId> first;
        for (uint32_t seed = 1; seed <= 300; ++seed)
        {
            std::vector<EntrantId> const offers = DrawOffers(s, PlayerId(1), 0, seed, none);
            CHECK(offers.size() == 3);
            CHECK(offers == DrawOffers(s, PlayerId(1), 0, seed, none));
            if (seed == 1)
                first = offers;
            else
                differs += offers != first ? 1 : 0;
            std::set<EntrantId> const unique(offers.begin(), offers.end());
            CHECK(unique.size() == offers.size());
            std::size_t previous = 0;
            for (EntrantId id : offers)
            {
                Entrant const* e = s.Find(id);
                CHECK(e && !e->IsPlayer() && e->enabled);
                std::size_t const index = IndexOf(order, id);
                CHECK(index + 5 >= place && index < place + 5);
                CHECK(index >= previous);
                previous = index;
                std::size_t const distance = index < place ? place - index : index - place + 1;
                closePicks += distance <= 2 ? 1 : 0;
                farPicks += distance >= 4 ? 1 : 0;
            }
            bothFactions += FactionMask(s, offers) == 3 ? 1 : 0;
            threeClasses += ClassCount(s, offers) == 3 ? 1 : 0;
        }
        CHECK(threeClasses == 300);
        CHECK(bothFactions >= 285);
        CHECK(differs > 200);
        CHECK(closePicks > farPicks * 2); // nearer places are likelier: distances 1 and 2 take about 60 %, 4 and 5 about 20 %
        std::printf("    300 draws: %d with both factions, offers at distance 1-2: %d, at 4-5: %d\n", bothFactions, closePicks, farPicks);

        // A player without an entry counts as rank 15, 0 stars, rating 1000 (and takes that place).
        LadderState withEntry = s;
        withEntry.entrants.erase(std::remove_if(withEntry.entrants.begin(), withEntry.entrants.end(),
                                                [](Entrant const& e) { return e.IsPlayer(); }), withEntry.entrants.end());
        LadderState without = withEntry;
        EnsurePlayer(withEntry, 7, "Seven");
        for (uint32_t seed = 1; seed <= 40; ++seed)
            CHECK(DrawOffers(without, PlayerId(7), 0, seed, none) == DrawOffers(withEntry, PlayerId(7), 0, seed, none));
        {
            std::vector<EntrantId> const bottom = NpcOrderOf(without);
            Entrant fresh;
            fresh.id = PlayerId(7);
            std::size_t const p = PlaceOf(without, fresh);
            for (uint32_t seed = 1; seed <= 40; ++seed)
                for (EntrantId id : DrawOffers(without, PlayerId(7), 0, seed, none))
                    CHECK(IndexOf(bottom, id) + 5 >= p && IndexOf(bottom, id) < p + 5);
        }

        // The last opponent and blocked NPCs are left out; a disabled NPC and other players are never offered.
        LadderState t = s;
        EntrantId const lastNpc = order[place];
        std::set<EntrantId> blocked = {order[place + 1], order[place - 1], order[place - 2]};
        t.Find(order[place + 2])->enabled = false;
        t.entrants.push_back(Player(2, 8, 2, 100000));
        for (uint32_t seed = 1; seed <= 200; ++seed)
            for (EntrantId id : DrawOffers(t, PlayerId(1), lastNpc, seed, blocked))
            {
                CHECK(id != lastNpc && !blocked.count(id) && id != order[place + 2] && !IsPlayerId(id));
                CHECK(DrawOffers(t, PlayerId(1), lastNpc, seed, blocked).size() == 3);
            }

        // The window widens, step by step, when it holds fewer than three candidates.
        std::set<EntrantId> wall;
        for (std::size_t i = place - 5; i < place + 5; ++i)
            wall.insert(order[i]);
        wall.erase(order[place]);
        wall.erase(order[place - 1]); // two stay
        for (uint32_t seed = 1; seed <= 60; ++seed)
        {
            std::vector<EntrantId> const offers = DrawOffers(s, PlayerId(1), 0, seed, wall);
            CHECK(offers.size() == 3);
            int inside = 0;
            for (EntrantId id : offers)
            {
                std::size_t const index = IndexOf(order, id);
                inside += index + 5 >= place && index < place + 5 ? 1 : 0;
                CHECK(!wall.count(id));
                CHECK(index + 6 >= place && index < place + 6); // one step wider: 6 above and 6 below
            }
            CHECK(inside >= 1 && inside <= 2);
        }
        // Everything but one blocked: one offer. Everything blocked: none.
        std::set<EntrantId> all(order.begin(), order.end());
        CHECK(DrawOffers(s, PlayerId(1), 0, 1, all).empty());
        all.erase(order[3]);
        CHECK(DrawOffers(s, PlayerId(1), 0, 1, all) == std::vector<EntrantId>{order[3]});
        // A short ladder: what exists. The last opponent only when no other NPC is left.
        LadderState two = OfferLadder(2);
        CHECK(DrawOffers(two, PlayerId(1), 0, 5, none).size() == 2);
        CHECK(DrawOffers(two, PlayerId(1), NpcId(1), 5, none) == std::vector<EntrantId>{NpcId(2)});
        LadderState one = OfferLadder(1);
        CHECK(DrawOffers(one, PlayerId(1), NpcId(1), 5, none) == std::vector<EntrantId>{NpcId(1)});
        CHECK(DrawOffers(one, PlayerId(1), 0, 5, none) == std::vector<EntrantId>{NpcId(1)});
        CHECK(DrawOffers(one, PlayerId(1), NpcId(1), 5, {NpcId(1)}).empty());
        CHECK(DrawOffers(OfferLadder(0), PlayerId(1), 0, 5, none).empty());
        CHECK(DrawOffers(s, PlayerId(1), 0, 5, none, 0).empty());
        CHECK(DrawOffers(s, PlayerId(1), 0, 5, none, 5).size() == 5);
        // Exactly three NPCs: all three, in ladder order.
        LadderState three = OfferLadder(3);
        CHECK(DrawOffers(three, PlayerId(1), 0, 9, none) == NpcOrderOf(three));

        // The ends of the ladder: Legend players are offered the top, rank 15 the bottom.
        LadderState top = OfferLadder(60);
        top.entrants.push_back(Player(1, kLegend, 0, 200000));
        std::vector<EntrantId> const topOrder = NpcOrderOf(top);
        for (uint32_t seed = 1; seed <= 40; ++seed)
            for (EntrantId id : DrawOffers(top, PlayerId(1), 0, seed, none))
                CHECK(IndexOf(topOrder, id) < 5);
        LadderState bottom = OfferLadder(60);
        bottom.entrants.push_back(Player(1, kBottomRank, 0, 1));
        std::vector<EntrantId> const bottomOrder = NpcOrderOf(bottom);
        for (uint32_t seed = 1; seed <= 40; ++seed)
            for (EntrantId id : DrawOffers(bottom, PlayerId(1), 0, seed, none))
                CHECK(IndexOf(bottomOrder, id) >= 55);

        // Classes first: when the window has fewer classes than offers, a class is repeated, but never while another
        // class is still in the window. A window of two classes only.
        LadderState twoClasses = OfferLadder(30);
        for (Entrant& e : twoClasses.entrants)
            e.heroClass = e.id % 2 ? "MAGE" : "ROGUE";
        for (uint32_t seed = 1; seed <= 40; ++seed)
            CHECK(ClassCount(twoClasses, DrawOffers(twoClasses, PlayerId(1), 0, seed, none)) == 2);

        // "any" counts as neither faction: a window of alliance and any NPCs still offers what exists.
        LadderState noHorde = OfferLadder(30);
        for (Entrant& e : noHorde.entrants)
            e.faction = e.id % 2 ? Faction::Alliance : Faction::Any;
        for (uint32_t seed = 1; seed <= 20; ++seed)
            CHECK(DrawOffers(noHorde, PlayerId(1), 0, seed, none).size() == 3);
    }

    void TestOffersRealRoster()
    {
        // The real roster on a fresh ladder (everybody on rank 15): the player stands behind all NPCs, so the five
        // last NPCs are the window, wider when blocked.
        LadderState s = RealRoster(Finder().size(), 31337);
        CHECK(s.entrants.size() == Finder().size());
        std::string sql;
        CHECK(ReadFile(g_dir + "/../data/sql/world/0049_hearthstone_ladder_tables.sql", sql));
        std::vector<std::vector<std::string>> blockRows;
        std::size_t const at = sql.find("INSERT IGNORE INTO `hearthstone_ladder_block`");
        CHECK(at != std::string::npos);
        std::set<EntrantId> blocked;
        for (std::vector<std::string> const& r : ParseTuples(sql, sql.find("VALUES", at) + 6))
            blocked.insert(NpcId(static_cast<uint32_t>(std::strtoul(r[0].c_str(), nullptr, 10))));
        CHECK(blocked.size() == 8);
        std::vector<EntrantId> const order = NpcOrderOf(s);
        // Block the NPCs on the last three places on top: the window moves up.
        for (std::size_t i = order.size() - 3; i < order.size(); ++i)
            blocked.insert(order[i]);
        for (uint32_t seed = 1; seed <= 100; ++seed)
        {
            std::vector<EntrantId> const offers = DrawOffers(s, PlayerId(3), 0, OfferSeed(s.seed, 3, seed), blocked);
            CHECK(offers.size() == 3);
            CHECK(ClassCount(s, offers) == 3);
            for (EntrantId id : offers)
                CHECK(!blocked.count(id) && IndexOf(order, id) + 15 >= order.size());
        }
    }

    void TestOfferSets()
    {
        std::set<EntrantId> blocked;
        LadderState s = OfferLadder(60);
        s.roundsPlayed = 42;

        // The seed follows the ladder seed, the account and the draw counter.
        CHECK(OfferSeed(11, 1, 1) == OfferSeed(11, 1, 1));
        CHECK(OfferSeed(11, 1, 1) != OfferSeed(11, 1, 2));
        CHECK(OfferSeed(11, 1, 1) != OfferSeed(11, 2, 1));
        CHECK(OfferSeed(11, 1, 1) != OfferSeed(12, 1, 1));
        CHECK(OfferSeed(11, 1, 2) != OfferSeed(11, 2, 1));
        CHECK(OfferSeed(11, 1, 1) != RoundSeed(11, 1));

        // The first draw: counter 1, three slots, the round. A valid set is kept.
        OfferSet set;
        CHECK(!OfferSetValid(set, s, blocked)); // empty
        CHECK(EnsureOffers(set, s, 1, blocked));
        CHECK(set.account == 1 && set.drawCounter == 1 && set.round == 42 && set.slots.size() == 3 && set.lastOpponent == 0);
        CHECK(set.slots == DrawOffers(s, PlayerId(1), 0, OfferSeed(s.seed, 1, 1), blocked));
        OfferSet const keep = set;
        s.roundsPlayed = 43;
        CHECK(!EnsureOffers(set, s, 1, blocked));
        CHECK(set.slots == keep.slots && set.drawCounter == 1 && set.round == 42);

        // A reroll: counter 2, new offers (not every time, but over many accounts), reproducible from the same
        // history on a second copy of the state.
        int changed = 0;
        for (uint32_t account = 1; account <= 40; ++account)
        {
            OfferSet a, b;
            CHECK(EnsureOffers(a, s, account, blocked) && EnsureOffers(b, s, account, blocked));
            CHECK(a.slots == b.slots);
            OfferSet const before = a;
            RedrawOffers(a, s, account, blocked);
            RedrawOffers(b, s, account, blocked);
            CHECK(a.drawCounter >= 2 && a.drawCounter == b.drawCounter && a.slots == b.slots && a.slots.size() == 3);
            CHECK(a.round == 43);
            changed += a.slots != before.slots ? 1 : 0;
        }
        // A reroll never hands out the same set again (a player without an entry stands at the bottom, where only
        // five places are near, so the first draw alone would repeat now and then).
        CHECK(changed == 40);

        // A set that cannot be shown any more is redrawn: a left or blocked NPC, an unknown id, a player id, a
        // repeated slot, an empty set, the set of another account.
        auto invalid = [&](OfferSet bad, char const* what) {
            (void)what;
            CHECK(!OfferSetValid(bad, s, blocked));
        };
        OfferSet good = keep;
        CHECK(OfferSetValid(good, s, blocked));
        OfferSet bad = good;
        bad.slots[1] = NpcId(5000);
        invalid(bad, "unknown");
        bad = good;
        bad.slots[2] = PlayerId(1);
        invalid(bad, "player");
        bad = good;
        bad.slots[2] = bad.slots[0];
        invalid(bad, "repeated");
        bad = good;
        bad.slots.clear();
        invalid(bad, "empty");
        LadderState left = s;
        left.Find(good.slots[1])->enabled = false;
        CHECK(!OfferSetValid(good, left, blocked));
        std::set<EntrantId> const nowBlocked = {good.slots[2]};
        CHECK(!OfferSetValid(good, s, nowBlocked));
        OfferSet redraw = good;
        CHECK(EnsureOffers(redraw, left, 1, blocked));
        CHECK(redraw.drawCounter == 2 && OfferSetValid(redraw, left, blocked));
        for (EntrantId id : redraw.slots)
            CHECK(id != good.slots[1]);
        redraw = good;
        CHECK(EnsureOffers(redraw, s, 1, nowBlocked));
        for (EntrantId id : redraw.slots)
            CHECK(id != good.slots[2]);
        redraw = good;
        CHECK(EnsureOffers(redraw, s, 2, blocked)); // another account
        CHECK(redraw.account == 2 && redraw.drawCounter == 1 && redraw.lastOpponent == 0 && redraw.slots.size() == 3);

        // After a ladder match: the opponent is remembered, the slots are cleared and the counter stays; the next
        // sit-down draws again without that NPC, from the ladder as it is by then.
        OfferSet after = good;
        EntrantId const opponent = good.slots[0];
        OffersAfterMatch(after, opponent);
        CHECK(after.lastOpponent == opponent && after.slots.empty() && after.drawCounter == 1 && after.account == 1);
        CHECK(!OfferSetValid(after, s, blocked));
        for (uint32_t round = 43; round < 60; ++round)
        {
            LadderState later = s;
            later.roundsPlayed = round;
            OfferSet next = after;
            CHECK(EnsureOffers(next, later, 1, blocked));
            CHECK(next.drawCounter == 2 && next.round == round && next.slots.size() == 3 && next.lastOpponent == opponent);
            for (EntrantId id : next.slots)
                CHECK(id != opponent);
        }

        // Nothing to offer: an empty set (not valid, so asked again later), the counter still counts the draw.
        LadderState empty;
        OfferSet nothing;
        CHECK(EnsureOffers(nothing, empty, 3, blocked) && nothing.slots.empty() && nothing.drawCounter == 1);
    }

    // ---- tests: offer rows ---------------------------------------------------------------------------------

    // The two offer tables as the statements of OfferSaveSql leave them.
    struct OfferMock
    {
        std::map<uint32_t, std::map<uint32_t, std::vector<std::string>>> slots; // account -> slot -> cells
        std::map<uint32_t, std::vector<std::string>> states;                    // account -> cells
        std::vector<std::string> log;

        bool Run(std::string const& sql)
        {
            log.push_back(sql.substr(0, 14));
            std::string const deleteOffer = std::string("DELETE FROM `") + kTableOffer + "` WHERE `account_id` = ";
            std::string const replaceOffer = std::string("REPLACE INTO `") + kTableOffer + "` (" + kOfferColumns + ") VALUES ";
            std::string const upsertState = std::string("INSERT INTO `") + kTableOfferState + "` (" + kOfferStateColumns + ") VALUES ";
            if (sql.compare(0, deleteOffer.size(), deleteOffer) == 0)
            {
                slots.erase(static_cast<uint32_t>(std::strtoul(sql.c_str() + deleteOffer.size(), nullptr, 10)));
                return true;
            }
            if (sql.compare(0, replaceOffer.size(), replaceOffer) == 0)
            {
                for (std::vector<std::string> const& c : ParseTuples(sql, replaceOffer.size()))
                    slots[static_cast<uint32_t>(std::strtoul(c[0].c_str(), nullptr, 10))]
                         [static_cast<uint32_t>(std::strtoul(c[1].c_str(), nullptr, 10))] = c;
                return true;
            }
            if (sql.compare(0, upsertState.size(), upsertState) == 0 && sql.find(" ON DUPLICATE KEY UPDATE `draw_counter` = ") != std::string::npos)
            {
                std::vector<std::vector<std::string>> const rows = ParseTuples(sql, upsertState.size());
                if (rows.size() != 1)
                    return false;
                states[static_cast<uint32_t>(std::strtoul(rows[0][0].c_str(), nullptr, 10))] = rows[0];
                return true;
            }
            return false;
        }

        bool Run(std::vector<std::string> const& statements)
        {
            for (std::string const& sql : statements)
                if (!Run(sql))
                    return false;
            return true;
        }

        // What the store's LoadOffers does.
        bool Load(uint32_t account, bool& found, OfferSet& out, std::string& error)
        {
            found = false;
            auto const st = states.find(account);
            if (st == states.end())
                return true;
            OfferStateRow state;
            if (!OfferStateRowFromCells(st->second, state))
                return false;
            std::vector<OfferRow> rows;
            for (auto const& kv : slots[account])
            {
                OfferRow row;
                if (!OfferRowFromCells(kv.second, row))
                    return false;
                rows.push_back(row);
            }
            found = BuildOfferSet(state, rows, out, error);
            return found;
        }
    };

    void TestOfferRows()
    {
        OfferMock db;
        bool found = true;
        OfferSet loaded;
        std::string error;
        CHECK(db.Load(4, found, loaded, error) && !found);

        OfferSet set;
        set.account = 4;
        set.slots = {NpcId(11), NpcId(2000000), NpcId(7)};
        set.round = 123456;
        set.drawCounter = 9;
        set.lastOpponent = NpcId(5);
        std::vector<std::string> const sql = OfferSaveSql(set);
        CHECK(sql.size() == 3);
        CHECK(sql[0] == "DELETE FROM `hearthstone_ladder_offer` WHERE `account_id` = 4");
        CHECK(sql[1].compare(0, 40, "REPLACE INTO `hearthstone_ladder_offer` ") == 0);
        CHECK(sql[2].find("ON DUPLICATE KEY UPDATE") != std::string::npos);
        CHECK(db.Run(sql));
        CHECK(db.Load(4, found, loaded, error) && found);
        CHECK(loaded.account == 4 && loaded.slots == set.slots && loaded.round == 123456 && loaded.drawCounter == 9 &&
              loaded.lastOpponent == NpcId(5));

        // The state and slot rows hold the same values as the struct.
        OfferStateRow const stateRow = ToStateRow(set);
        CHECK(stateRow.account == 4 && stateRow.drawCounter == 9 && stateRow.drawnRound == 123456 && stateRow.lastOpponent == 5);
        std::vector<OfferRow> const slotRows = ToOfferRows(set);
        CHECK(slotRows.size() == 3 && slotRows[1].slot == 1 && slotRows[1].npc == 2000000 && slotRows[2].account == 4);

        // Another account does not disturb it; saving again replaces the slots (no stale slot is left).
        OfferSet other = set;
        other.account = 5;
        other.slots = {NpcId(1)};
        CHECK(db.Run(OfferSaveSql(other)));
        OfferSet again = set;
        OffersAfterMatch(again, set.slots[1]);
        CHECK(db.Run(OfferSaveSql(again)));
        CHECK(db.slots.count(4) == 0 || db.slots[4].empty());
        CHECK(db.Load(4, found, loaded, error) && found && loaded.slots.empty() && loaded.drawCounter == 9 &&
              loaded.lastOpponent == NpcId(2000000));
        CHECK(db.Load(5, found, loaded, error) && found && loaded.slots == std::vector<EntrantId>{NpcId(1)});
        // Two slots instead of three.
        again.slots = {NpcId(8), NpcId(9)};
        again.drawCounter = 10;
        CHECK(db.Run(OfferSaveSql(again)));
        CHECK(db.Load(4, found, loaded, error) && found && loaded.slots == again.slots && loaded.drawCounter == 10);
        CHECK(db.slots[4].size() == 2);
        // Nothing to write for the slots of an empty set: only the delete and the state row.
        OfferSet empty;
        empty.account = 6;
        CHECK(OfferSaveSql(empty).size() == 2);
        // More slots than the table holds are cut.
        OfferSet many = set;
        many.slots = {1, 2, 3, 4, 5};
        CHECK(ToOfferRows(many).size() == static_cast<std::size_t>(kOfferCount));
        // The reset deletes both tables.
        std::vector<std::string> const reset = OfferResetSql();
        CHECK(reset.size() == 2 && reset[0] == "DELETE FROM `hearthstone_ladder_offer`" &&
              reset[1] == "DELETE FROM `hearthstone_ladder_offer_state`");

        // Rows that are not a set.
        OfferStateRow st = stateRow;
        OfferSet out;
        CHECK(BuildOfferSet(st, slotRows, out, error) && out.slots == set.slots);
        std::vector<OfferRow> bad = slotRows;
        bad[1].account = 5;
        CHECK(!BuildOfferSet(st, bad, out, error) && error.find("account") != std::string::npos);
        bad = slotRows;
        bad[2].slot = 3;
        CHECK(!BuildOfferSet(st, bad, out, error) && error.find("slot") != std::string::npos);
        bad = slotRows;
        bad[2].slot = 1;
        CHECK(!BuildOfferSet(st, bad, out, error) && error.find("twice") != std::string::npos);
        bad = slotRows;
        bad.erase(bad.begin() + 1); // slots 0 and 2
        CHECK(!BuildOfferSet(st, bad, out, error) && error.find("missing") != std::string::npos);
        bad = {slotRows[2], slotRows[0], slotRows[1]}; // any order
        CHECK(BuildOfferSet(st, bad, out, error) && out.slots == set.slots);
        CHECK(BuildOfferSet(st, {}, out, error) && out.slots.empty());

        OfferRow row;
        CHECK(OfferRowFromCells({"4", "2", "4294967297"}, row) && row.slot == 2 && row.npc == 4294967297ull);
        CHECK(!OfferRowFromCells({"4", "2"}, row) && !OfferRowFromCells({"4", "x", "7"}, row) && !OfferRowFromCells({"-1", "2", "7"}, row));
        OfferStateRow stRow;
        CHECK(OfferStateRowFromCells({"4", "9", "100", "5"}, stRow) && stRow.drawCounter == 9 && stRow.drawnRound == 100 && stRow.lastOpponent == 5);
        CHECK(!OfferStateRowFromCells({"4", "9", "100"}, stRow) && !OfferStateRowFromCells({"4", "9", "1.5", "5"}, stRow));
    }

    void TestOffersMigration()
    {
        std::string sql;
        CHECK(ReadFile(g_dir + "/../data/sql/character/0016_hearthstone_ladder_offer.sql", sql));
        struct Table
        {
            char const* name;
            char const* columns;
            std::size_t count;
            bool timestamp;
        };
        Table const tables[] = {
            {kTableOfferState, kOfferStateColumns, kOfferStateColumnCount, true},
            {kTableOffer, kOfferColumns, kOfferColumnCount, false},
        };
        for (Table const& t : tables)
        {
            std::size_t const pos = sql.find(std::string("CREATE TABLE IF NOT EXISTS `") + t.name + "` (");
            CHECK(pos != std::string::npos);
            if (pos == std::string::npos)
                continue;
            std::size_t const end = sql.find(") ENGINE=", pos);
            std::istringstream in(sql.substr(pos, end - pos));
            std::string line;
            std::vector<std::string> names;
            while (std::getline(in, line))
                if (line.size() >= 4 && line.compare(0, 3, "  `") == 0)
                    names.push_back(line.substr(3, line.find('`', 3) - 3));
            std::vector<std::string> const wanted = SplitColumns(t.columns);
            CHECK(wanted.size() == t.count);
            if (t.timestamp)
            {
                CHECK(names.size() == wanted.size() + 1 && names.back() == "updated_at");
                names.resize(wanted.size());
            }
            CHECK(names == wanted);
        }
        CHECK(sql.find("PRIMARY KEY (`account_id`, `slot`)") != std::string::npos);
        // Additive and safe to run twice; the rules of the migration headers.
        CHECK(sql.find("DROP ") == std::string::npos && sql.find("ALTER ") == std::string::npos);
        std::size_t creates = 0, guarded = 0;
        for (std::size_t p = sql.find("\nCREATE TABLE"); p != std::string::npos; p = sql.find("\nCREATE TABLE", p + 1))
            ++creates;
        for (std::size_t p = sql.find("\nCREATE TABLE IF NOT EXISTS"); p != std::string::npos;
             p = sql.find("\nCREATE TABLE IF NOT EXISTS", p + 1))
            ++guarded;
        CHECK(creates == 2 && guarded == 2);
        CHECK(sql.find('\\') == std::string::npos);
        std::istringstream in(sql);
        std::string line;
        while (std::getline(in, line))
            if (line.compare(0, 2, "--") == 0)
                CHECK(line.find(';') == std::string::npos);
    }

    // ---- tests: the ladder duels that pause and resume -----------------------------------------------

    std::string MockEscape(std::string const& text)
    {
        std::string out;
        for (char c : text)
        {
            if (c == '\'')
                out += "''";
            else
                out.push_back(c);
        }
        return out;
    }

    std::vector<std::string> DeckOf(char const* prefix, int n)
    {
        std::vector<std::string> deck;
        for (int i = 0; i < n; ++i)
            deck.push_back(std::string(prefix) + "_" + std::to_string(i / 2));
        return deck;
    }

    DuelRow SampleDuel()
    {
        MatchStartParams params;
        params.guid = 77;
        params.humanName = "Swifty";
        params.humanClass = "MAGE";
        params.humanDeck = DeckOf("CS2", 30);
        params.opponentName = "Amaz's Rival";
        params.opponentClass = "WARRIOR";
        params.opponentDeck = DeckOf("EX1", 30);
        params.seed = 4000000001u;
        params.profile = Match::AiProfile::Hard;
        params.meta.accountId = 9;
        params.meta.deckId = 5;
        params.meta.deckName = "Mage's deck";
        params.meta.opponentDeckKey = "lib_warrior_3";
        params.meta.startedAt = 1790000000;
        return DuelFromParams(params, 12, 4321, 7, "abcdef");
    }

    // The cells of the first "(a, 'b', ...)" tuple after VALUES in one INSERT (quotes removed, '' undone).
    std::vector<std::string> InsertCells(std::string const& sql)
    {
        std::vector<std::string> cells;
        std::size_t pos = sql.find("VALUES (");
        if (pos == std::string::npos)
            return cells;
        pos += 8;
        std::string cell;
        bool quoted = false;
        for (; pos < sql.size(); ++pos)
        {
            char const c = sql[pos];
            if (quoted)
            {
                if (c == '\'' && pos + 1 < sql.size() && sql[pos + 1] == '\'')
                {
                    cell.push_back('\'');
                    ++pos;
                }
                else if (c == '\'')
                    quoted = false;
                else
                    cell.push_back(c);
            }
            else if (c == '\'')
                quoted = true;
            else if (c == ',' || c == ')')
            {
                while (!cell.empty() && cell[0] == ' ')
                    cell.erase(cell.begin());
                cells.push_back(cell);
                cell.clear();
                if (c == ')')
                    break;
            }
            else
                cell.push_back(c);
        }
        return cells;
    }

    void TestDuelRows()
    {
        CHECK(!IsLadderRun(0) && !IsLadderRun(4000000000ull));
        CHECK(IsLadderRun(RunOfDuel(12)) && DuelIdOfRun(RunOfDuel(12)) == 12);

        // Row -> statement -> cells -> row.
        DuelRow const row = SampleDuel();
        CHECK(row.account == 9 && row.guid == 77 && row.npcEntry == 4321 && row.profile == "hard" && row.state == kDuelActive);
        std::vector<std::string> const cells = InsertCells(DuelInsertSql(row, MockEscape));
        CHECK(cells.size() == kDuelColumnCount);
        DuelRow back;
        CHECK(DuelRowFromCells(cells, back));
        CHECK(back.id == row.id && back.account == row.account && back.guid == row.guid && back.state == row.state);
        CHECK(back.npcEntry == row.npcEntry && back.opponentName == "Amaz's Rival" && back.opponentClass == "WARRIOR");
        CHECK(back.opponentDeckKey == row.opponentDeckKey && back.opponentDeck == row.opponentDeck && back.humanDeck == row.humanDeck);
        CHECK(back.profile == "hard" && back.deckId == 5 && back.deckName == "Mage's deck" && back.humanClass == "MAGE");
        CHECK(back.seed == 4000000001u && back.startedAt == 1790000000 && back.rulesVersion == 7 && back.datasetHash == "abcdef");
        CHECK(back.timerMs == 0);

        // Malformed rows are refused.
        {
            std::vector<std::string> bad = cells;
            bad.pop_back();
            DuelRow out;
            CHECK(!DuelRowFromCells(bad, out));
            bad = cells;
            bad[3] = "running";
            CHECK(!DuelRowFromCells(bad, out));
            bad = cells;
            bad[9] = "boss";
            CHECK(!DuelRowFromCells(bad, out));
            bad = cells;
            bad[0] = "0";
            CHECK(!DuelRowFromCells(bad, out));
            bad = cells;
            bad[0] = std::to_string(kLadderRunBit + 1);
            CHECK(!DuelRowFromCells(bad, out));
            bad = cells;
            bad[8] = "A,,B";
            CHECK(!DuelRowFromCells(bad, out));
            bad = cells;
            bad[13] = "A B";
            CHECK(!DuelRowFromCells(bad, out));
            bad = cells;
            bad[18] = "-5";
            CHECK(!DuelRowFromCells(bad, out));
        }

        // The cards.
        {
            std::vector<std::string> cards;
            CHECK(SplitCards("", cards) && cards.empty());
            CHECK(SplitCards("A", cards) && cards.size() == 1);
            CHECK(SplitCards("EX1_001,CS2_020,NEW1-5", cards) && cards.size() == 3);
            CHECK(JoinCards(cards) == "EX1_001,CS2_020,NEW1-5");
            CHECK(!SplitCards(",A", cards) && !SplitCards("A,", cards) && !SplitCards("A;B", cards));
        }

        // The match parameters and the suspended duel of a stored row.
        MatchStartParams const params = ParamsFromDuel(row, 88, "Another");
        CHECK(params.guid == 88 && params.humanName == "Another" && params.arena && params.ladder &&
              params.arenaRunId == RunOfDuel(12));
        CHECK(params.humanDeck == row.humanDeck && params.opponentDeck == row.opponentDeck && params.seed == row.seed);
        CHECK(params.profile == Match::AiProfile::Hard && params.difficulty == "hard");
        CHECK(params.meta.accountId == 9 && params.meta.opponentEntry == 4321 && params.meta.deckId == 5);
        DuelRow normal = row;
        normal.profile = "normal";
        CHECK(ParamsFromDuel(normal, 1, "x").profile == Match::AiProfile::Normal && ParamsFromDuel(normal, 1, "x").difficulty == "normal");
        DuelRow timed = row;
        timed.timerMs = 4200;
        std::vector<LoggedAction> log(3);
        log[1].side = 1;
        SuspendedDuel const duel = SuspendedFromDuel(timed, 88, "Another", log);
        CHECK(duel.log.size() == 3 && duel.timerMs == 4200 && duel.rulesVersion == 7 && duel.datasetHash == "abcdef");
        CHECK(duel.params.arena && duel.params.guid == 88);
        CHECK(DuelReplayable(row, 7, "abcdef") && !DuelReplayable(row, 8, "abcdef") && !DuelReplayable(row, 7, "other"));

        // Long texts are cut to the columns.
        MatchStartParams longParams;
        longParams.opponentName = std::string(300, 'n');
        longParams.opponentClass = std::string(40, 'c');
        longParams.meta.deckName = std::string(300, 'd');
        longParams.meta.opponentDeckKey = std::string(80, 'k');
        DuelRow const cut = DuelFromParams(longParams, 1, 1, 1, "h");
        CHECK(cut.opponentName.size() == kNameBytes && cut.opponentClass.size() == kClassBytes &&
              cut.deckName.size() == kNameBytes && cut.opponentDeckKey.size() == kDeckKeyBytes);
    }

    void TestDuelLogAndSql()
    {
        // A log round trips through its rows.
        std::vector<LoggedAction> log;
        LoggedAction a;
        a.side = 1;
        a.action.type = Match::ActionType::Mulligan;
        a.action.replace = {5, 7};
        log.push_back(a);
        LoggedAction b;
        b.side = 0;
        b.action.type = Match::ActionType::Play;
        b.action.source = 12;
        log.push_back(b);
        LoggedAction c;
        c.side = 0;
        c.action.type = Match::ActionType::EndTurn;
        log.push_back(c);
        bool ok = false;
        std::string const insert = DuelActionsInsertSql(12, 4, log, ok);
        CHECK(ok);
        CHECK(insert.find("INSERT INTO `hearthstone_ladder_duel_action`") == 0);
        CHECK(insert.find("(12, 4, 'v1,1,M,0,0,-1,0,5,7')") != std::string::npos);
        CHECK(insert.find("(12, 5, 'v1,0,P,12,0,-1,0')") != std::string::npos);
        CHECK(insert.find("(12, 6, 'v1,0,E,0,0,-1,0')") != std::string::npos);
        std::vector<std::string> rows = {"v1,1,M,0,0,-1,0,5,7", "v1,0,P,12,0,-1,0", "v1,0,E,0,0,-1,0"};
        std::vector<LoggedAction> decoded;
        std::size_t failedAt = 99;
        std::string error;
        CHECK(DecodeDuelLog(rows, decoded, failedAt, error) && decoded.size() == 3);
        CHECK(SameLoggedAction(decoded[0], log[0]) && SameLoggedAction(decoded[1], log[1]) && SameLoggedAction(decoded[2], log[2]));
        rows[1] = "v9,0,P,12,0,-1,0";
        CHECK(!DecodeDuelLog(rows, decoded, failedAt, error) && failedAt == 1 && !error.empty());
        // Nothing to write; a side outside 0..1 does not encode.
        CHECK(DuelActionsInsertSql(12, 0, {}, ok).empty() && ok);
        LoggedAction badSide;
        badSide.side = 2;
        CHECK(DuelActionsInsertSql(12, 0, {badSide}, ok).empty() && !ok);

        // The pause statements: the state and the timer; a failed store trims the log first.
        std::vector<std::string> pause = DuelPauseSql(12, 4200, false, 0);
        CHECK(pause.size() == 1);
        CHECK(pause[0] == "UPDATE `hearthstone_ladder_duel` SET `state` = 'paused', `timer_ms` = 4200 WHERE `duel_id` = 12");
        pause = DuelPauseSql(12, 0, true, 6);
        CHECK(pause.size() == 2);
        CHECK(pause[0] == "DELETE FROM `hearthstone_ladder_duel_action` WHERE `duel_id` = 12 AND `idx` >= 6");
        CHECK(DuelActiveSql(12, 77) ==
              "UPDATE `hearthstone_ladder_duel` SET `state` = 'active', `guid` = 77, `timer_ms` = 0 WHERE `duel_id` = 12");

        // The settlement: the duel and its log go in one list of statements (a GM result deletes no duel).
        std::vector<std::string> const settle = SettleSql(12);
        CHECK(settle.size() == 2);
        CHECK(settle[0] == "DELETE FROM `hearthstone_ladder_duel_action` WHERE `duel_id` = 12");
        CHECK(settle[1] == "DELETE FROM `hearthstone_ladder_duel` WHERE `duel_id` = 12");
        CHECK(SettleSql(0).empty());
        CHECK(DuelDeleteSql(12).size() == 2 && DuelResetSql().size() == 3);

        // The queue is stored as a whole: the DELETE of every row, then one INSERT of the rows in order.
        QueueRow round;
        round.excludedNpc = 4321;
        QueueRow result;
        result.kind = kQueueResult;
        result.account = 7;
        result.playerName = "O'Brien";
        result.npcId = 900;
        result.result = 2;
        result.duelId = 12;
        std::vector<std::string> const none = QueueSnapshotSql({}, EscapeSql);
        CHECK(none.size() == 1 && none[0] == "DELETE FROM `hearthstone_ladder_queue`");
        std::vector<std::string> const snap = QueueSnapshotSql({round, result}, EscapeSql);
        CHECK(snap.size() == 2 && snap[0] == none[0]);
        CHECK(snap[1] == "INSERT INTO `hearthstone_ladder_queue` (`kind`, `excluded_npc`, `account_id`, `player_name`, "
            "`npc_id`, `result`, `duel_id`) VALUES (0, 4321, 0, '', 0, 0, 0), (1, 0, 7, 'O\\'Brien', 900, 2, 12)");

        QueueRow row;
        CHECK(QueueRowFromCells({"5", "0", "4321", "0", "", "0", "0", "0"}, row) && row.seq == 5 && row.kind == kQueueRound &&
            row.excludedNpc == 4321);
        CHECK(QueueRowFromCells({"6", "1", "0", "7", "Nina", "900", "2", "12"}, row) && row.kind == kQueueResult &&
            row.account == 7 && row.playerName == "Nina" && row.npcId == 900 && row.result == 2 && row.duelId == 12);
        CHECK(!QueueRowFromCells({"5", "0", "4321"}, row) && !QueueRowFromCells({"x", "0", "1", "0", "", "0", "0", "0"}, row));
        CHECK(!QueueRowFromCells({"5", "2", "0", "0", "", "0", "0", "0"}, row));  // unknown kind
        CHECK(!QueueRowFromCells({"5", "1", "0", "7", "Nina", "900", "3", "12"}, row));  // unknown result

        DuelRow paused = SampleDuel();
        paused.state = kDuelPaused;
        CHECK(DuelText(paused, 12) == "paused duel against Amaz's Rival (Warrior, 12 action(s))");
    }

    void TestDuelMigration()
    {
        std::string sql;
        CHECK(ReadFile(g_dir + "/../data/sql/character/0017_hearthstone_ladder_duel.sql", sql));
        struct Table
        {
            char const* name;
            char const* columns;
            std::size_t count;
            bool timestamp;
        };
        Table const tables[] = {
            {kTableDuel, kDuelColumns, kDuelColumnCount, true},
            {kTableDuelAction, "`duel_id`, `idx`, `action`", 3, false},
        };
        for (Table const& t : tables)
        {
            std::size_t const pos = sql.find(std::string("CREATE TABLE IF NOT EXISTS `") + t.name + "` (");
            CHECK(pos != std::string::npos);
            if (pos == std::string::npos)
                continue;
            std::size_t const end = sql.find(") ENGINE=", pos);
            std::istringstream in(sql.substr(pos, end - pos));
            std::string line;
            std::vector<std::string> names;
            while (std::getline(in, line))
                if (line.size() >= 4 && line.compare(0, 3, "  `") == 0)
                    names.push_back(line.substr(3, line.find('`', 3) - 3));
            std::vector<std::string> const wanted = SplitColumns(t.columns);
            CHECK(wanted.size() == t.count);
            if (t.timestamp)
            {
                CHECK(names.size() == wanted.size() + 1 && names.back() == "updated_at");
                names.resize(wanted.size());
            }
            CHECK(names == wanted);
        }
        // One ladder duel per account; the action column holds the longest encoded action.
        CHECK(sql.find("UNIQUE KEY `uq_hearthstone_ladder_duel_account` (`account_id`)") != std::string::npos);
        CHECK(sql.find("`action` VARCHAR(" + std::to_string(kDuelActionBytes) + ")") != std::string::npos);
        CHECK(sql.find("DROP ") == std::string::npos && sql.find("ALTER ") == std::string::npos);
        std::size_t creates = 0, guarded = 0;
        for (std::size_t p = sql.find("\nCREATE TABLE"); p != std::string::npos; p = sql.find("\nCREATE TABLE", p + 1))
            ++creates;
        for (std::size_t p = sql.find("\nCREATE TABLE IF NOT EXISTS"); p != std::string::npos;
             p = sql.find("\nCREATE TABLE IF NOT EXISTS", p + 1))
            ++guarded;
        CHECK(creates == 3 && guarded == 3);
        CHECK(sql.find('\\') == std::string::npos);
        std::istringstream in(sql);
        std::string line;
        while (std::getline(in, line))
            if (line.compare(0, 2, "--") == 0)
                CHECK(line.find(';') == std::string::npos);
    }

    // Migration 0019: the queue replaces the pending table of 0017 (its rows are copied in order, then the old table is dropped).
    void TestQueueMigration()
    {
        std::string sql;
        CHECK(ReadFile(g_dir + "/../data/sql/character/0019_hearthstone_ladder_queue.sql", sql));
        CHECK(sql.find("CREATE TABLE IF NOT EXISTS `hearthstone_ladder_queue` (") != std::string::npos);
        std::size_t const pos = sql.find("CREATE TABLE IF NOT EXISTS `hearthstone_ladder_queue` (");
        std::size_t const end = sql.find(") ENGINE=", pos);
        std::istringstream in(sql.substr(pos, end - pos));
        std::string line;
        std::vector<std::string> names;
        while (std::getline(in, line))
            if (line.size() >= 4 && line.compare(0, 3, "  `") == 0)
                names.push_back(line.substr(3, line.find('`', 3) - 3));
        CHECK(names == SplitColumns(kQueueColumns));
        CHECK(SplitColumns(kQueueColumns).size() == kQueueColumnCount);
        CHECK(sql.find("INSERT INTO `hearthstone_ladder_queue` (`kind`, `excluded_npc`) SELECT 0, `excluded_npc` FROM "
            "`hearthstone_ladder_pending` ORDER BY `seq`;") != std::string::npos);
        CHECK(sql.find("DROP TABLE IF EXISTS `hearthstone_ladder_pending`;") != std::string::npos);
        CHECK(sql.find("ALTER ") == std::string::npos);
        CHECK(sql.find('\\') == std::string::npos);
        std::istringstream lines(sql);
        while (std::getline(lines, line))
            if (line.compare(0, 2, "--") == 0)
                CHECK(line.find(';') == std::string::npos);
    }

    // ---- tests: the world data of the chairs ---------------------------------------------------------

    // The rows of the "INSERT [IGNORE] INTO `table`" statement of the 0049 migration.
    std::vector<std::vector<std::string>> WorldFileRows(std::string const& sql, std::string const& table)
    {
        std::size_t at = sql.find("INSERT INTO `" + table + "`");
        if (at == std::string::npos)
            at = sql.find("INSERT IGNORE INTO `" + table + "`");
        if (at == std::string::npos)
            return {};
        std::size_t const values = sql.find("VALUES", at);
        return values == std::string::npos ? std::vector<std::vector<std::string>>() : ParseTuples(sql, values + 6);
    }

    float F(std::string const& text)
    {
        return static_cast<float>(std::strtod(text.c_str(), nullptr));
    }

    struct FileWorld
    {
        std::vector<WorldTableRow> tables;
        std::vector<WorldPathRow> paths;
        std::vector<WorldLineRow> lines;
        std::vector<WorldBlockRow> blocks;
        std::string sql;
    };

    // The 0049 migration read like the server reads the tables.
    bool ReadFileWorld(FileWorld& w)
    {
        if (!ReadFile(g_dir + "/../data/sql/world/0049_hearthstone_ladder_tables.sql", w.sql))
            return false;
        for (std::vector<std::string> const& c : WorldFileRows(w.sql, kWorldTable))
        {
            WorldTableRow r;
            if (!WorldTableRowFromCells(c, r))
                return false;
            w.tables.push_back(r);
        }
        for (std::vector<std::string> const& c : WorldFileRows(w.sql, kWorldPath))
        {
            WorldPathRow r;
            if (!WorldPathRowFromCells(c, r))
                return false;
            w.paths.push_back(r);
        }
        for (std::vector<std::string> const& c : WorldFileRows(w.sql, kWorldLine))
        {
            WorldLineRow r;
            if (!WorldLineRowFromCells(c, r))
                return false;
            w.lines.push_back(r);
        }
        for (std::vector<std::string> const& c : WorldFileRows(w.sql, kWorldBlock))
        {
            WorldBlockRow r;
            if (!WorldBlockRowFromCells(c, r))
                return false;
            w.blocks.push_back(r);
        }
        return true;
    }

    void TestWorldMigration()
    {
        FileWorld file;
        CHECK(ReadFileWorld(file));
        std::string const& sql = file.sql;
        std::string squashed; // one line, single spaces
        for (char c : sql)
        {
            if (c == '\n' || c == ' ')
            {
                if (!squashed.empty() && squashed.back() != ' ')
                    squashed.push_back(' ');
            }
            else
                squashed.push_back(c);
        }

        // The columns of the four tables, in the order of the readers.
        struct Table
        {
            char const* name;
            char const* columns;
            std::size_t count;
        };
        Table const tables[] = {
            {kWorldTable, kWorldTableColumns, kWorldTableColumnCount},
            {kWorldPath, kWorldPathColumns, kWorldPathColumnCount},
            {kWorldLine, kWorldLineColumns, kWorldLineColumnCount},
            {kWorldBlock, kWorldBlockColumns, kWorldBlockColumnCount},
        };
        for (Table const& t : tables)
        {
            std::size_t const pos = sql.find(std::string("CREATE TABLE IF NOT EXISTS `") + t.name + "` (");
            CHECK(pos != std::string::npos);
            if (pos == std::string::npos)
                continue;
            std::size_t const end = sql.find(") ENGINE=", pos);
            std::istringstream in(sql.substr(pos, end - pos));
            std::string line;
            std::vector<std::string> names;
            while (std::getline(in, line))
                if (line.size() >= 4 && line.compare(0, 3, "  `") == 0)
                    names.push_back(line.substr(3, line.find('`', 3) - 3));
            std::vector<std::string> const wanted = SplitColumns(t.columns);
            CHECK(wanted.size() == t.count && names == wanted);
            // The INSERT of the file names the same columns.
            CHECK(squashed.find(std::string("INTO `") + t.name + "` (" + t.columns + ")") != std::string::npos);
        }
        auto varchar = [&](char const* table, char const* column) -> std::size_t {
            std::size_t pos = sql.find(std::string("CREATE TABLE IF NOT EXISTS `") + table + "`");
            pos = sql.find(std::string("  `") + column + "` VARCHAR(", pos);
            return pos == std::string::npos ? 0 : static_cast<std::size_t>(std::atoi(sql.c_str() + pos + std::strlen(column) + 13));
        };
        CHECK(varchar(kWorldTable, "city") == kCityBytes);
        CHECK(varchar(kWorldLine, "text") == kLineBytes);
        CHECK(varchar(kWorldBlock, "reason") == kReasonBytes);
        CHECK(sql.find("ENUM('win','loss','draw')") != std::string::npos);

        // The rules of the migration headers: no backslash, no semicolon in a comment, re-runnable statements.
        CHECK(sql.find('\\') == std::string::npos);
        std::istringstream in(sql);
        std::string line;
        while (std::getline(in, line))
            if (line.compare(0, 2, "--") == 0)
                CHECK(line.find(';') == std::string::npos);
        CHECK(sql.find("DROP ") == std::string::npos && sql.find("ALTER ") == std::string::npos);
        CHECK(sql.find("INSERT IGNORE INTO `hearthstone_ladder_line`") != std::string::npos);
        CHECK(sql.find("INSERT IGNORE INTO `hearthstone_ladder_block`") != std::string::npos);
        CHECK(sql.find("DELETE FROM `hearthstone_ladder_table` WHERE `table_guid` IN (7003001, 7003002)") != std::string::npos);
        CHECK(sql.find("DELETE FROM `hearthstone_ladder_path` WHERE `table_guid` IN (7003001, 7003002)") != std::string::npos);

        // The chair template: a copy of 101766 with the ladder script, and the spawns of the two player chairs.
        CHECK(sql.find("FROM `gameobject_template` WHERE `entry` = 101766") != std::string::npos);
        CHECK(sql.find(std::string("'") + kLadderChairScript + "'") != std::string::npos);
        CHECK(sql.find("DELETE FROM `gameobject_template` WHERE `entry` = " + std::to_string(kLadderChairEntry)) != std::string::npos);
        CHECK(sql.find("919100, `type`, `displayId`") != std::string::npos);
        for (int i = 0; i < 24; ++i)
            CHECK(sql.find("`data" + std::to_string(i) + "`") != std::string::npos);
        std::size_t const selectAt = sql.find("SELECT\n  919100");
        CHECK(selectAt != std::string::npos);
        for (int i = 0; i < 24 && selectAt != std::string::npos; ++i)
            CHECK(sql.find("`data" + std::to_string(i) + "`", selectAt) != std::string::npos);
        std::vector<std::vector<std::string>> const chairs = WorldFileRows(sql, "gameobject");
        CHECK(chairs.size() == 2); // the INSERT INTO form: the two player chairs
        std::size_t const ignoreAt = sql.find("INSERT IGNORE INTO `gameobject`");
        CHECK(ignoreAt != std::string::npos);
        std::vector<std::vector<std::string>> const opponents = ParseTuples(sql, sql.find("VALUES", ignoreAt) + 6);
        CHECK(opponents.size() == 2);
        std::map<uint32_t, std::string> entryOf;
        for (std::vector<std::string> const& c : chairs)
        {
            CHECK(c.size() == 17 && c[2] == "1");
            entryOf[static_cast<uint32_t>(std::strtoul(c[0].c_str(), nullptr, 10))] = c[1];
        }
        for (std::vector<std::string> const& c : opponents)
        {
            CHECK(c.size() == 17 && c[2] == "1");
            entryOf[static_cast<uint32_t>(std::strtoul(c[0].c_str(), nullptr, 10))] = c[1];
        }
        CHECK(entryOf.size() == 4 && entryOf[7003005] == "919100" && entryOf[7003007] == "919100" &&
              entryOf[7003006] == "101766" && entryOf[7003008] == "101766");

        // The owner data of the two tables.
        LadderWorld world;
        std::vector<std::string> problems;
        BuildWorld(file.tables, file.paths, file.lines, file.blocks, world, problems);
        CHECK(problems.empty());
        for (std::string const& p : problems)
            std::printf("    problem: %s\n", p.c_str());
        CHECK(world.tables.size() == 2);
        LadderTable const* tb = world.FindByTable(7003001);
        LadderTable const* da = world.FindByTable(7003002);
        CHECK(tb && da);
        if (tb && da)
        {
            CHECK(tb->city == "Thunder Bluff" && tb->mapId == 1 && tb->chairGuid == 7003007 && tb->opponentChairGuid == 7003008);
            CHECK(tb->spawn.x == F("-1224.166748") && tb->spawn.y == F("144.446808") && tb->spawn.z == F("133.451202") &&
                  tb->spawnO == F("1.465406"));
            CHECK(tb->stop.x == F("-1222.630859") && tb->stop.y == F("151.279099") && tb->stop.z == F("133.160339"));
            CHECK(tb->path.empty() && tb->Route().size() == 2);
            CHECK(da->city == "Darnassus" && da->mapId == 1 && da->chairGuid == 7003005 && da->opponentChairGuid == 7003006);
            CHECK(da->spawn.x == F("9821.464844") && da->spawn.y == F("2341.206787") && da->spawn.z == F("1321.681885") &&
                  da->spawnO == F("1.997904"));
            CHECK(da->stop.x == F("9821.241211") && da->stop.y == F("2350.254639") && da->stop.z == F("1321.681396"));
            CHECK(da->path.size() == 1 && da->path[0].x == F("9819.243164") && da->path[0].y == F("2346.087158") &&
                  da->path[0].z == F("1321.681885") && da->Route().size() == 3);
            // About 7 yards in Thunder Bluff and 10 in all in Darnassus (owner doc).
            CHECK(tb->RouteLength() > 6.5f && tb->RouteLength() < 7.5f);
            CHECK(da->RouteLength() > 9.5f && da->RouteLength() < 10.5f);
            // The chairs are the gameobjects of the file.
            for (LadderTable const* t : {tb, da})
            {
                CHECK(entryOf.count(t->chairGuid) && entryOf[t->chairGuid] == std::to_string(kLadderChairEntry));
                CHECK(entryOf.count(t->opponentChairGuid) && entryOf[t->opponentChairGuid] == "101766");
            }
            CHECK(world.FindByChair(7003005) == da && world.FindByChair(7003007) == tb && !world.FindByChair(7003006) &&
                  !world.FindByTable(1));
        }

        // The drafted lines: six win, six loss, three draw, all with text that fits and has no stray characters.
        CHECK(world.lines.size() == 15 && world.LineCount(LineKind::Win) == 6 && world.LineCount(LineKind::Loss) == 6 &&
              world.LineCount(LineKind::Draw) == 3);
        std::set<std::string> texts;
        for (LadderLine const& l : world.lines)
        {
            CHECK(!l.text.empty() && l.text.size() <= kLineBytes && texts.insert(l.text).second);
            CHECK(l.text.find('"') == std::string::npos && l.text.find(';') == std::string::npos && l.text.find('%') == std::string::npos);
            CHECK(l.text.find("  ") == std::string::npos && l.text[0] >= 'A' && l.text[0] <= 'Z');
        }
        // Picking: deterministic, every line of a kind comes up, only lines of that kind.
        std::set<uint32_t> picked;
        for (uint32_t seed = 1; seed <= 200; ++seed)
        {
            LadderLine const* a = world.PickLine(LineKind::Loss, seed);
            LadderLine const* b = world.PickLine(LineKind::Loss, seed);
            CHECK(a && a == b && a->kind == LineKind::Loss);
            picked.insert(a->id);
            CHECK(world.PickLine(LineKind::Win, seed)->kind == LineKind::Win && world.PickLine(LineKind::Draw, seed)->kind == LineKind::Draw);
        }
        CHECK(picked.size() == 6);

        // The blocked roster NPCs: all in the finder list, each with a reason that fits.
        CHECK(world.blocked.size() == 8 && world.blockRows.size() == 8);
        std::set<uint32_t> finderEntries;
        for (FinderName const& n : Finder())
            finderEntries.insert(n.entry);
        for (WorldBlockRow const& b : world.blockRows)
        {
            CHECK(finderEntries.count(b.creatureEntry) == 1);
            CHECK(!b.reason.empty() && b.reason.size() <= kReasonBytes && world.blocked.count(NpcId(b.creatureEntry)) == 1);
        }
    }

    void TestWorldBuild()
    {
        auto table = [](uint32_t guid, uint32_t chair, uint32_t opponent, char const* city = "Town") {
            WorldTableRow r;
            r.tableGuid = guid;
            r.city = city;
            r.mapId = 1;
            r.chairGuid = chair;
            r.opponentChairGuid = opponent;
            r.spawnX = 1;
            r.stopX = 4;
            return r;
        };
        auto path = [](uint32_t guid, uint32_t no, double x) {
            WorldPathRow r;
            r.tableGuid = guid;
            r.pointNo = no;
            r.x = x;
            return r;
        };
        auto line = [](uint32_t id, LineKind kind, char const* text) {
            WorldLineRow r;
            r.id = id;
            r.kind = kind;
            r.text = text;
            return r;
        };
        std::vector<WorldLineRow> const lines = {line(2, LineKind::Win, "w"), line(1, LineKind::Loss, "l"), line(3, LineKind::Draw, "d")};
        LadderWorld w;
        std::vector<std::string> problems;

        // Order of the rows does not matter; the result is in table_guid / point_no / id order.
        BuildWorld({table(20, 21, 22, "B"), table(10, 11, 12, "A")}, {path(20, 2, 3), path(10, 1, 2), path(20, 1, 9)},
                   lines, {WorldBlockRow{7, "x"}, WorldBlockRow{3, "y"}}, w, problems);
        CHECK(problems.empty());
        CHECK(w.tables.size() == 2 && w.tables[0].tableGuid == 10 && w.tables[1].tableGuid == 20);
        CHECK(w.tables[0].path.size() == 1 && w.tables[0].path[0].x == 2.0f);
        CHECK(w.tables[1].path.size() == 2 && w.tables[1].path[0].x == 9.0f && w.tables[1].path[1].x == 3.0f);
        CHECK(w.tables[1].Route().size() == 4 && w.tables[1].Route().front().x == 1.0f && w.tables[1].Route().back().x == 4.0f);
        CHECK(w.tables[1].RouteLength() == 8.0f + 6.0f + 1.0f); // 1 -> 9 -> 3 -> 4 on the x axis
        CHECK(w.lines.size() == 3 && w.lines[0].id == 1 && w.lines[2].id == 3);
        CHECK(w.blocked == std::set<EntrantId>({NpcId(3), NpcId(7)}) && w.blockRows[0].creatureEntry == 3 && w.blockRows[0].reason == "y");

        // What is left out, and why.
        auto build = [&](std::vector<WorldTableRow> const& t, std::vector<WorldPathRow> const& p) {
            problems.clear();
            BuildWorld(t, p, lines, {}, w, problems);
        };
        build({table(10, 11, 12), table(10, 13, 14)}, {});
        CHECK(w.tables.size() == 1 && problems.size() == 1); // the same table twice
        build({table(10, 11, 12), table(20, 11, 22)}, {});
        CHECK(w.tables.size() == 1 && w.tables[0].tableGuid == 10 && problems.size() == 1); // a shared chair
        build({table(10, 11, 11)}, {});
        CHECK(w.tables.empty() && problems.size() == 1);
        build({table(10, 10, 12)}, {});
        CHECK(w.tables.empty() && problems.size() == 1);
        build({table(10, 0, 12)}, {});
        CHECK(w.tables.empty() && problems.size() == 1);
        build({table(0, 11, 12)}, {});
        CHECK(w.tables.empty() && problems.size() == 1);
        build({table(10, 11, 12, "")}, {});
        CHECK(w.tables.empty() && problems.size() == 1);
        WorldTableRow nan = table(10, 11, 12);
        nan.stopY = std::nan("");
        build({nan}, {});
        CHECK(w.tables.empty() && problems.size() == 1);
        WorldTableRow inf = table(10, 11, 12);
        inf.spawnZ = HUGE_VAL;
        build({inf}, {});
        CHECK(w.tables.empty() && problems.size() == 1);
        build({table(10, 11, 12, "A city name that is far longer than the column holds")}, {});
        CHECK(w.tables.size() == 1 && problems.size() == 1); // kept, reported
        build({table(10, 11, 12), table(20, 21, 22)}, {path(10, 1, 2), path(10, 1, 3)});
        CHECK(w.tables.size() == 1 && w.tables[0].tableGuid == 20 && problems.size() == 1); // point 1 twice
        build({table(10, 11, 12)}, {path(99, 1, 2)});
        CHECK(w.tables.size() == 1 && w.tables[0].path.empty() && problems.size() == 1); // no such table
        WorldPathRow badPoint = path(10, 1, 2);
        badPoint.y = std::nan("");
        build({table(10, 11, 12)}, {badPoint});
        CHECK(w.tables.empty() && problems.size() == 1);

        // Lines and blocks.
        problems.clear();
        BuildWorld({}, {}, {line(1, LineKind::Win, "a"), line(1, LineKind::Win, "b"), line(2, LineKind::Loss, ""),
                            line(3, LineKind::Loss, std::string(300, 'x').c_str())},
                   {WorldBlockRow{0, "zero"}, WorldBlockRow{5, "a"}, WorldBlockRow{5, "b"}}, w, problems);
        CHECK(w.lines.size() == 2 && w.lines[0].text == "a" && w.lines[1].id == 3);
        CHECK(w.blocked == std::set<EntrantId>({NpcId(5)}) && w.blockRows.size() == 1 && w.blockRows[0].reason == "a");
        CHECK(problems.size() == 6); // twice, no text, too long, no draw line, zero entry, blocked twice
        CHECK(w.PickLine(LineKind::Draw, 1) == nullptr && w.LineCount(LineKind::Draw) == 0);

        // Line kinds.
        CHECK(std::string(LineKindCode(LineKind::Win)) == "win" && std::string(LineKindCode(LineKind::Loss)) == "loss" &&
              std::string(LineKindCode(LineKind::Draw)) == "draw");
        LineKind kind = LineKind::Draw;
        CHECK(ParseLineKind("win", kind) && kind == LineKind::Win && ParseLineKind("loss", kind) && kind == LineKind::Loss);
        CHECK(!ParseLineKind("lose", kind) && !ParseLineKind("", kind) && !ParseLineKind("Win", kind));
        CHECK(LineKindForPlayerResult(Result::Win) == LineKind::Loss && LineKindForPlayerResult(Result::Loss) == LineKind::Win &&
              LineKindForPlayerResult(Result::Draw) == LineKind::Draw);

        // The cells of the tables.
        WorldTableRow tr;
        std::vector<std::string> const cells = {"7003001", "Thunder Bluff", "1", "7003007", "7003008", "-1224.166748", "144.446808",
                                                "133.451202", "1.465406", "-1222.630859", "151.279099", "133.160339"};
        CHECK(WorldTableRowFromCells(cells, tr) && tr.tableGuid == 7003001 && tr.city == "Thunder Bluff" && tr.spawnX == -1224.166748);
        std::vector<std::string> broken = cells;
        broken[5] = "12.5x";
        CHECK(!WorldTableRowFromCells(broken, tr));
        broken = cells;
        broken[5] = "";
        CHECK(!WorldTableRowFromCells(broken, tr));
        broken = cells;
        broken[0] = "-1";
        CHECK(!WorldTableRowFromCells(broken, tr));
        broken = cells;
        broken[2] = "70000000000";
        CHECK(!WorldTableRowFromCells(broken, tr));
        broken = cells;
        broken.pop_back();
        CHECK(!WorldTableRowFromCells(broken, tr));
        broken = cells;
        broken[8] = "1e-05";
        CHECK(WorldTableRowFromCells(broken, tr) && tr.spawnO == 1e-05);
        WorldPathRow pr;
        CHECK(WorldPathRowFromCells({"7003002", "1", "9819.243164", "2346.087158", "1321.681885"}, pr) && pr.pointNo == 1);
        CHECK(!WorldPathRowFromCells({"7003002", "1", "9819.243164", "2346.087158"}, pr) && !WorldPathRowFromCells({"7003002", "x", "1", "2", "3"}, pr));
        WorldLineRow lr;
        CHECK(WorldLineRowFromCells({"4", "draw", "text"}, lr) && lr.kind == LineKind::Draw && lr.text == "text");
        CHECK(!WorldLineRowFromCells({"4", "tie", "text"}, lr) && !WorldLineRowFromCells({"4", "draw"}, lr));
        WorldBlockRow br;
        CHECK(WorldBlockRowFromCells({"6177", "why"}, br) && br.creatureEntry == 6177 && br.reason == "why");
        CHECK(!WorldBlockRowFromCells({"abc", "why"}, br) && !WorldBlockRowFromCells({"6177"}, br));
    }

    void TestRosterRisks()
    {
        std::vector<RosterRisk> risks;
        std::set<EntrantId> blocked = {NpcId(2)};
        CHECK(RosterRiskSummary(risks, blocked).empty());
        auto risk = [](uint32_t entry, char const* name, char const* script, char const* ai) {
            RosterRisk r;
            r.entry = entry;
            r.name = name;
            r.scriptName = script;
            r.aiName = ai;
            return r;
        };
        risks.push_back(risk(1, "One", "npc_one", ""));
        risks.push_back(risk(2, "Two", "boss_two", "EventAI"));
        risks.push_back(risk(3, "Three", "", "EventAI"));
        std::string const text = RosterRiskSummary(risks, blocked);
        CHECK(text.find("2 roster NPC(s) with a script of their own (1 blocked)") != std::string::npos);
        CHECK(text.find("2 with an AI name (1 blocked)") != std::string::npos);
        CHECK(text.find("One (1, npc_one)") != std::string::npos && text.find("Two") == std::string::npos &&
              text.find("Three") == std::string::npos);
        CHECK(RosterRiskSummary(risks, {NpcId(1), NpcId(2)}).find("not blocked") == std::string::npos);
        // The list is cut.
        std::vector<RosterRisk> many;
        for (uint32_t i = 1; i <= 20; ++i)
            many.push_back(risk(i, "N", "s", ""));
        std::string const cut = RosterRiskSummary(many, {}, 5);
        CHECK(cut.find("(5, s)") != std::string::npos && cut.find("(6, s)") == std::string::npos && cut.find("...") != std::string::npos);
    }

    // ---- the chair window and the walk-up (src/ladder/HsLadderPlay.*) -------------------------------

    void TestPlayResults()
    {
        Result r = Result::Draw;
        CHECK(PlayerResultOfMatch("win", r) && r == Result::Win);
        CHECK(PlayerResultOfMatch("loss", r) && r == Result::Loss); // a surrender is a loss (why concede)
        CHECK(PlayerResultOfMatch("draw", r) && r == Result::Draw);
        // An interruption (logout, teleport, death, the NPC gone, Stuck, a GM) is no result.
        r = Result::Win;
        CHECK(!PlayerResultOfMatch("int", r) && r == Result::Win);
        CHECK(!PlayerResultOfMatch("", r) && !PlayerResultOfMatch("-", r) && !PlayerResultOfMatch("WIN", r) &&
              !PlayerResultOfMatch("surrender", r));
    }

    void TestPlayTexts()
    {
        CHECK(RecordText(3, 1, 0) == "3-1-0" && RecordText(0, 0, 0) == "0-0-0");
        CHECK(PositionText(7, 3) == "Rank 7 (3 stars)" && PositionText(10, 1) == "Rank 10 (1 star)" &&
              PositionText(15, 0) == "Rank 15 (0 stars)" && PositionText(kLegend, 0) == "Legend");
        CHECK(FactionText(Faction::Alliance) == "Alliance" && FactionText(Faction::Horde) == "Horde" &&
              FactionText(Faction::Any) == "Neutral");
        CHECK(StandingText(true, 12, 2, 4, 3, 1) == "Your standing: Rank 12 (2 stars), record 4-3-1.");
        CHECK(StandingText(true, kLegend, 0, 1, 0, 0) == "Your standing: Legend, record 1-0-0.");
        CHECK(StandingText(false, 3, 3, 9, 9, 9) == "You are not on the ladder yet (you start at Rank 25 (0 stars)).");
        // One offer: name - class - faction - rank - the record.
        CHECK(OfferLineText("Jaina Proudmoore", "MAGE", Faction::Alliance, 9, 4, 2, 1, 0) ==
              "Jaina Proudmoore - Mage - Alliance - Rank 9 (4 stars) - your record 2-1-0");
        CHECK(OfferLineText("Thrall", "SHAMAN", Faction::Horde, kLegend, 0, 0, 0, 0) ==
              "Thrall - Shaman - Horde - Legend - your record 0-0-0");
        CHECK(OfferLineText("X", "WARLOCK", Faction::Any, 15, 0, 0, 0, 3) ==
              "X - Warlock - Neutral - Rank 15 (0 stars) - your record 0-0-3");
        // Cut: the name goes first, the rest stays whole; never more than the limit.
        std::string const longName(200, 'n');
        std::string const cut = OfferLineText(longName, "PRIEST", Faction::Horde, 1, 5, 12, 11, 10, 100);
        CHECK(cut.size() == 100 && cut.find(" - Priest - Horde - Rank 1 (5 stars) - your record 12-11-10") != std::string::npos);
        CHECK(cut.compare(0, 4, "nnnn") == 0);
        CHECK(OfferLineText("Name", "MAGE", Faction::Horde, 1, 1, 1, 1, 1, 10).size() <= 10);
        // A name with a multi-byte character is not cut in the middle of it.
        std::string const utf = std::string(60, 'a') + "\xC3\xA9" + std::string(60, 'b'); // an e with an acute accent
        std::string const utfCut = OfferLineText(utf, "ROGUE", Faction::Alliance, 5, 2, 0, 0, 0, 100);
        CHECK(utfCut.size() <= 100);
        bool valid = true;
        for (std::size_t i = 0; i < utfCut.size(); ++i)
        {
            unsigned char const c = static_cast<unsigned char>(utfCut[i]);
            if (c == 0xC3 && (i + 1 >= utfCut.size() || (static_cast<unsigned char>(utfCut[i + 1]) & 0xC0) != 0x80))
                valid = false;
        }
        CHECK(valid);
        // A cut inside the accent: the character is dropped whole, never half of it.
        std::string const edge = OfferLineText(std::string(10, 'a') + "\xC3\xA9", "MAGE", Faction::Horde, 1, 1, 0, 0, 0,
                                               std::string(" - Mage - Horde - Rank 1 (1 star) - your record 0-0-0").size() + 11);
        CHECK(edge.compare(0, 10, std::string(10, 'a')) == 0 && edge.find(" - Mage") == 10);
    }

    LadderTable PlayTable(bool withPath)
    {
        LadderTable t;
        t.tableGuid = 7003002;
        t.city = "Darnassus";
        t.mapId = 1;
        t.spawn = WorldPoint{0, 0, 0};
        t.spawnO = 1.5f;
        if (withPath)
            t.path.push_back(WorldPoint{3, 4, 0});
        t.stop = WorldPoint{6, 8, 0};
        return t;
    }

    void TestPlayWalk()
    {
        for (bool path : {true, false})
        {
            LadderTable const t = PlayTable(path);
            WorldPoint const seat{7, 8, 0};
            std::vector<WorldPoint> const in = WalkInPoints(t, seat);
            // path points, the stop point, the seat: the spawn point is where the NPC starts.
            CHECK(in.size() == (path ? 3u : 2u));
            CHECK(Distance(in.back(), seat) < 0.001f && Distance(in[in.size() - 2], t.stop) < 0.001f);
            if (path)
                CHECK(Distance(in[0], t.path[0]) < 0.001f);
            // Without a seat of its own (it is the stop point) there is no extra leg.
            std::vector<WorldPoint> const same = WalkInPoints(t, t.stop);
            CHECK(same.size() == in.size() - 1 && Distance(same.back(), t.stop) < 0.001f);
            // The way back: the legs passed in reverse, the spawn point last.
            std::vector<WorldPoint> const all = WalkBackPoints(t, in, in.size());
            CHECK(all.size() == in.size() + 1 && Distance(all.front(), seat) < 0.001f && Distance(all.back(), t.spawn) < 0.001f);
            for (std::size_t i = 0; i < in.size(); ++i)
                CHECK(Distance(all[i], in[in.size() - 1 - i]) < 0.001f);
            // Cancelled on the first leg: straight back to the spawn point.
            std::vector<WorldPoint> const none = WalkBackPoints(t, in, 0);
            CHECK(none.size() == 1 && Distance(none[0], t.spawn) < 0.001f);
            // Cancelled after one leg: that point, then the spawn point.
            std::vector<WorldPoint> const one = WalkBackPoints(t, in, 1);
            CHECK(one.size() == 2 && Distance(one[0], in[0]) < 0.001f && Distance(one[1], t.spawn) < 0.001f);
            // More legs than there are are cut to them.
            CHECK(WalkBackPoints(t, in, 99).size() == all.size());
        }
        CHECK(std::fabs(Distance(WorldPoint{0, 0, 0}, WorldPoint{3, 4, 0}) - 5.0f) < 0.0001f);
    }

    // The texts of the chair window: the migration has the three npc_text rows of the code and follows the rules of
    // the other migrations (re-runnable, no backslash, no semicolon in a comment or a string).
    void TestChairTextsMigration()
    {
        std::string sql;
        CHECK(ReadFile(g_dir + "/../data/sql/world/0050_hearthstone_ladder_chair_texts.sql", sql));
        std::string const ids[] = {std::to_string(kChairTextOffers), std::to_string(kChairTextDecks),
                                   std::to_string(kChairTextConfirm)};
        CHECK(sql.find("DELETE FROM `broadcast_text` WHERE `entry` IN (9195930, 9195931, 9195932)") != std::string::npos);
        CHECK(sql.find("DELETE FROM `npc_text` WHERE `ID` IN (9195930, 9195931, 9195932)") != std::string::npos);
        for (std::string const& id : ids)
        {
            CHECK(sql.find("(" + id + ",") != std::string::npos);                // the broadcast_text row
            CHECK(sql.find("(" + id + ", " + id + ", 1)") != std::string::npos); // the npc_text row
        }
        CHECK(sql.find('\\') == std::string::npos);
        std::istringstream in(sql);
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.compare(0, 2, "--") == 0)
                CHECK(line.find(';') == std::string::npos);
            else if (line.find('"') != std::string::npos)
            {
                std::size_t const open = line.find('"');
                std::size_t const close = line.rfind('"');
                CHECK(close > open && line.substr(open, close - open).find(';') == std::string::npos);
            }
        }
        CHECK(sql.find("DROP ") == std::string::npos && sql.find("ALTER ") == std::string::npos &&
              sql.find("INSERT IGNORE") == std::string::npos);
        // The ids are not used by the other ladder migration (0047: 9195920 to 9195923).
        std::string committee;
        CHECK(ReadFile(g_dir + "/../data/sql/world/0047_hearthstone_league_committee.sql", committee));
        for (std::string const& id : ids)
            CHECK(committee.find(id) == std::string::npos);
    }

    // The milestones (no reward yet) and their table (character migration 0018).
    void TestMilestones()
    {
        CHECK(MilestonesFor(1, true).empty() && MilestonesFor(15, false).empty() && MilestonesFor(5, true).empty());
        std::vector<std::string> const legend = MilestonesFor(kLegend, false);
        CHECK(legend.size() == 1 && legend[0] == kMilestoneLegend);
        std::vector<std::string> const first = MilestonesFor(kLegend, true);
        CHECK(first.size() == 2 && first[0] == kMilestoneLegend && first[1] == kMilestoneLegendFirst);
        CHECK(MilestoneInsertSql(9, kMilestoneLegend) ==
              "INSERT IGNORE INTO `hearthstone_ladder_milestone` (`account_id`, `milestone`) VALUES (9, 'legend')");
        CHECK(MilestoneText(kMilestoneLegend) == "You reached Legend.");
        CHECK(MilestoneText(kMilestoneLegendFirst) == "You are the number one of Legend." && MilestoneText("x").empty());

        std::string sql;
        CHECK(ReadFile(g_dir + "/../data/sql/character/0018_hearthstone_ladder_milestone.sql", sql));
        std::size_t const pos = sql.find(std::string("CREATE TABLE IF NOT EXISTS `") + kTableMilestone + "` (");
        CHECK(pos != std::string::npos);
        CHECK(sql.find("PRIMARY KEY (`account_id`, `milestone`)") != std::string::npos);
        CHECK(sql.find("`milestone` VARCHAR(16)") != std::string::npos);
        CHECK(sql.find("DROP ") == std::string::npos && sql.find("ALTER ") == std::string::npos && sql.find('\\') == std::string::npos);
        std::istringstream in(sql);
        std::string line;
        while (std::getline(in, line))
            if (line.compare(0, 2, "--") == 0)
                CHECK(line.find(';') == std::string::npos);
    }

    // The League Committee turns neutral, the dummy breadcrumb quest and the tutorial pages (world migration 0052).
    void TestBreadcrumbMigration()
    {
        std::string sql;
        CHECK(ReadFile(g_dir + "/../data/sql/world/0052_hearthstone_ladder_breadcrumb.sql", sql));
        CHECK(sql.find("UPDATE `creature_template` SET `faction` = 35, `npc_flags` = 3 WHERE `entry` IN (920100, 920101)") != std::string::npos);
        CHECK(sql.find("INSERT INTO `creature_involvedrelation` (`id`, `quest`) VALUES (920100, 919970), (920101, 919970)") != std::string::npos);
        // No start relation: the module offers the quest (src/HsLadderBreadcrumb.cpp).
        CHECK(sql.find("INSERT INTO `creature_questrelation`") == std::string::npos);
        CHECK(sql.find("(919970, 2, -9100, 1, 0, 0, 0, 0,") != std::string::npos);
        // The pages: menus 65280 to 65285, texts 9195940 to 9195945, one chain of options.
        for (int i = 0; i < 6; ++i)
        {
            std::string const menu = std::to_string(65280 + i), text = std::to_string(9195940 + i);
            CHECK(sql.find("(" + menu + ", " + text + ")") != std::string::npos);
            CHECK(sql.find("(" + text + ", " + text + ", 1)") != std::string::npos);
            CHECK(sql.find("(" + text + ",") != std::string::npos);
        }
        CHECK(sql.find("(65270, 1, 0, 'How does the league work?', 1, 1, 65280)") != std::string::npos);
        CHECK(sql.find("(65271, 1, 0, 'How does the league work?', 1, 1, 65280)") != std::string::npos);
        for (int i = 0; i < 5; ++i)
            CHECK(sql.find("(" + std::to_string(65280 + i) + ", 0, 0, '") != std::string::npos &&
                  sql.find(", 1, 1, " + std::to_string(65281 + i) + ")") != std::string::npos);
        // The ids are the ones of this migration alone.
        for (char const* other : {"0047_hearthstone_league_committee.sql", "0050_hearthstone_ladder_chair_texts.sql",
                                  "0051_hearthstone_ladder_duel_texts.sql", "0045_hearthstone_naxx_quests.sql"})
        {
            std::string text;
            CHECK(ReadFile(g_dir + "/../data/sql/world/" + other, text));
            for (int i = 0; i < 6; ++i)
                CHECK(text.find(std::to_string(9195940 + i)) == std::string::npos &&
                      text.find("(" + std::to_string(65280 + i) + ",") == std::string::npos);
            CHECK(text.find("919970") == std::string::npos);
        }
        CHECK(sql.find('\\') == std::string::npos && sql.find("DROP ") == std::string::npos && sql.find("ALTER ") == std::string::npos);
        std::istringstream in(sql);
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.compare(0, 2, "--") == 0)
                CHECK(line.find(';') == std::string::npos);
            else if (line.find('"') != std::string::npos)
            {
                std::size_t const open = line.find('"');
                std::size_t const close = line.rfind('"');
                CHECK(close > open && line.substr(open, close - open).find(';') == std::string::npos);
            }
        }
    }

    // The two breadcrumb quests (Ezra -> The Firebat, Ibelin -> Amaz Chan) and the first conversation step (migration 0054).
    void TestBreadcrumbSplit()
    {
        std::string sql;
        CHECK(ReadFile(g_dir + "/../data/sql/world/0054_hearthstone_breadcrumb_talk.sql", sql));
        CHECK(sql.find("INSERT INTO `creature_involvedrelation` (`id`, `quest`) VALUES (920101, 919970), (920100, 919971)") != std::string::npos);
        CHECK(sql.find("INSERT INTO `creature_questrelation`") == std::string::npos);
        CHECK(sql.find("DELETE FROM `creature_involvedrelation` WHERE `quest` IN (919970, 919971)") != std::string::npos);
        std::size_t const a = sql.find("(919970, 2, -9100, 1,");
        std::size_t const b = sql.find("(919971, 2, -9100, 1,");
        std::size_t const end = sql.find("-- Ezra's quest is ended by");
        CHECK(a != std::string::npos && b != std::string::npos && end != std::string::npos && a < b && b < end);
        if (a != std::string::npos && b != std::string::npos && end != std::string::npos)
        {
            std::string const ezra = sql.substr(a, b - a), ibelin = sql.substr(b, end - b);
            // Ezra (Horde) points at The Firebat in Thunder Bluff and never names the other side's NPC or city.
            CHECK(ezra.find("The Firebat in Thunder Bluff") != std::string::npos);
            CHECK(ezra.find("Amaz") == std::string::npos && ezra.find("Darnassus") == std::string::npos);
            CHECK(ibelin.find("Amaz Chan in Darnassus") != std::string::npos);
            CHECK(ibelin.find("Firebat") == std::string::npos && ibelin.find("Thunder Bluff") == std::string::npos);
        }
        // The two page texts and their rows.
        for (char const* id : {"9195950", "9195951"})
        {
            CHECK(sql.find(std::string("(") + id + ",") != std::string::npos);
            CHECK(sql.find(std::string("(") + id + ", " + id + ", 1)") != std::string::npos);
        }
        CHECK(sql.find("DELETE FROM `npc_text` WHERE `ID` IN (9195950, 9195951)") != std::string::npos);
        // The page of the league does not name either committee NPC or city either.
        std::size_t const page = sql.find("(9195950,\n");
        if (page == std::string::npos)
            CHECK(sql.find("(9195950,") != std::string::npos);
        CHECK(sql.find('\\') == std::string::npos && sql.find("DROP ") == std::string::npos && sql.find("ALTER ") == std::string::npos);
        std::istringstream in(sql);
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.compare(0, 2, "--") == 0)
                CHECK(line.find(';') == std::string::npos);
            else if (line.find('"') != std::string::npos)
            {
                std::size_t const open = line.find('"');
                std::size_t const close = line.rfind('"');
                CHECK(close > open && line.substr(open, close - open).find(';') == std::string::npos);
            }
        }
        // 0052 still names the quest it first made: nothing else in the older migrations uses the new ids.
        std::string older;
        CHECK(ReadFile(g_dir + "/../data/sql/world/0052_hearthstone_ladder_breadcrumb.sql", older));
        CHECK(older.find("9195950") == std::string::npos && older.find("919971") == std::string::npos);
    }

    // The quest headers and "The Rules of the League" chain (world migration 0055).
    void TestRulesQuestMigration()
    {
        std::string sql;
        CHECK(ReadFile(g_dir + "/../data/sql/world/0055_hearthstone_ladder_rules_quest.sql", sql));
        // The headers: Orgrimmar for the Horde quest, Stormwind City for the Alliance quest, like the lesson and the arena quests.
        CHECK(sql.find("UPDATE `quest_template` SET `ZoneOrSort` = -9100, `MinLevel` = 10, `RequiredRaces` = 434, `NextQuestInChain` = 919972 WHERE `entry` = 919970;") !=
              std::string::npos);
        CHECK(sql.find("UPDATE `quest_template` SET `ZoneOrSort` = -9100, `MinLevel` = 10, `RequiredRaces` = 589, `NextQuestInChain` = 919973 WHERE `entry` = 919971;") !=
              std::string::npos);
        std::string intro;
        CHECK(ReadFile(g_dir + "/../data/sql/world/0008_hearthstone_intro_quests.sql", intro));
        CHECK(intro.find("(919704, 2, -9100, 10, 0, 434, 0, 2,") != std::string::npos);
        CHECK(intro.find("(919714, 2, -9100, 10, 0, 589, 0, 2,") != std::string::npos);
        // The second quests: event quests of the same zone and races, each after its own first quest.
        CHECK(sql.find("(919972, 2, -9100, 10, 0, 434, 0, 2, 919970,") != std::string::npos);
        CHECK(sql.find("(919973, 2, -9100, 10, 0, 589, 0, 2, 919971,") != std::string::npos);
        CHECK(sql.find("INSERT INTO `creature_questrelation` (`id`, `quest`) VALUES (920101, 919972), (920100, 919973)") != std::string::npos);
        CHECK(sql.find("INSERT INTO `creature_involvedrelation` (`id`, `quest`) VALUES (920101, 919972), (920100, 919973)") != std::string::npos);
        // The last tutorial click (option 0 of menu 65284) completes whichever quest the character has.
        CHECK(sql.find("(9195960, 0, 7, 919972, 0,") != std::string::npos && sql.find("(9195960, 0, 7, 919973, 0,") != std::string::npos);
        CHECK(sql.find("UPDATE `gossip_menu_option` SET `action_script_id` = 0 WHERE `menu_id` IN (65280, 65281, 65282, 65283, 65284, 65285) AND `action_script_id` = 9195960;") != std::string::npos);
        CHECK(sql.find("UPDATE `gossip_menu_option` SET `action_script_id` = 9195960 WHERE `menu_id` = 65284 AND `id` = 0;") != std::string::npos);
        // That option is the one of 0052 that leads to the last page (65285).
        std::string tutorial;
        CHECK(ReadFile(g_dir + "/../data/sql/world/0052_hearthstone_ladder_breadcrumb.sql", tutorial));
        CHECK(tutorial.find("(65284, 0, 0, 'And what does the champion earn?', 1, 1, 65285)") != std::string::npos);
        // Each text names only its own NPC (the Horde one never the Alliance NPC, and the other way round).
        std::size_t const a = sql.find("(919972, 2, -9100");
        std::size_t const b = sql.find("(919973, 2, -9100");
        std::size_t const end = sql.find("-- The second quests are given and ended by");
        CHECK(a != std::string::npos && b != std::string::npos && end != std::string::npos && a < b && b < end);
        if (a != std::string::npos && b != std::string::npos && end != std::string::npos)
        {
            std::string const horde = sql.substr(a, b - a), alliance = sql.substr(b, end - b);
            CHECK(horde.find("The Firebat") != std::string::npos && horde.find("Amaz") == std::string::npos);
            CHECK(alliance.find("Amaz Chan") != std::string::npos && alliance.find("Firebat") == std::string::npos);
        }
        // The entry fee: both quests require fifty gold (RewOrReqMoney, in copper, negative, follows NextQuestInChain by six zeros).
        CHECK(sql.find("919970, 0, 0, 0, 0, 0, 0, -500000, 0, 0, 0, 0, 0,") != std::string::npos);
        CHECK(sql.find("919971, 0, 0, 0, 0, 0, 0, -500000, 0, 0, 0, 0, 0,") != std::string::npos);
        // No other reward on either quest (every other reward column is 0 in the insert) and nothing outside the ids of this migration.
        CHECK(sql.find("RewItemId1") != std::string::npos);
        CHECK(sql.find("DELETE FROM `quest_template` WHERE `entry` IN (919972, 919973);") != std::string::npos);
        CHECK(sql.find('\\') == std::string::npos && sql.find("DROP ") == std::string::npos && sql.find("ALTER ") == std::string::npos);
        std::istringstream in(sql);
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.compare(0, 2, "--") == 0)
                CHECK(line.find(';') == std::string::npos);
            else if (line.find('"') != std::string::npos)
            {
                std::size_t const open = line.find('"');
                std::size_t const close = line.rfind('"');
                CHECK(close > open && line.substr(open, close - open).find(';') == std::string::npos);
            }
        }
    }

    // The texts of the paused-duel window (world migration 0051).
    void TestDuelTextsMigration()
    {
        std::string sql;
        CHECK(ReadFile(g_dir + "/../data/sql/world/0051_hearthstone_ladder_duel_texts.sql", sql));
        std::string const ids[] = {std::to_string(kChairTextPaused), std::to_string(kChairTextSurrender)};
        CHECK(ids[0] == "9195933" && ids[1] == "9195934");
        CHECK(sql.find("DELETE FROM `broadcast_text` WHERE `entry` IN (9195933, 9195934)") != std::string::npos);
        CHECK(sql.find("DELETE FROM `npc_text` WHERE `ID` IN (9195933, 9195934)") != std::string::npos);
        for (std::string const& id : ids)
        {
            CHECK(sql.find("(" + id + ",") != std::string::npos);
            CHECK(sql.find("(" + id + ", " + id + ", 1)") != std::string::npos);
        }
        CHECK(sql.find('\\') == std::string::npos);
        std::istringstream in(sql);
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.compare(0, 2, "--") == 0)
                CHECK(line.find(';') == std::string::npos);
            else if (line.find('"') != std::string::npos)
            {
                std::size_t const open = line.find('"');
                std::size_t const close = line.rfind('"');
                CHECK(close > open && line.substr(open, close - open).find(';') == std::string::npos);
            }
        }
        CHECK(sql.find("DROP ") == std::string::npos && sql.find("ALTER ") == std::string::npos &&
              sql.find("INSERT IGNORE") == std::string::npos);
        // The ids are not used by the other ladder text migrations.
        std::string chair, committee;
        CHECK(ReadFile(g_dir + "/../data/sql/world/0050_hearthstone_ladder_chair_texts.sql", chair));
        CHECK(ReadFile(g_dir + "/../data/sql/world/0047_hearthstone_league_committee.sql", committee));
        for (std::string const& id : ids)
            CHECK(chair.find(id) == std::string::npos && committee.find(id) == std::string::npos);
    }
}

int main(int argc, char** argv)
{
    if (argc > 1)
        g_dir = argv[1];

    RUN(TestStarBorders);
    RUN(TestDraw);
    RUN(TestStreakBonus);
    RUN(TestLegendPushOutNpc);
    RUN(TestLegendPushOutPlayer);
    RUN(TestElo);
    RUN(TestLadderOrder);
    RUN(TestRosterRealNames);
    RUN(TestRosterRotation);
    RUN(TestPlacement);
    RUN(TestLadderStartFake);
    RUN(TestPairing);
    RUN(TestNoRepeat);
    RUN(TestRotatingBye);
    RUN(TestApplyRound);
    RUN(TestFailedGames);
    RUN(TestThreadsEqualSerial);
    RUN(TestRealGames);
    RUN(TestFailedRealGame);
    RUN(TestReplayAfterCrash);
    RUN(TestPlayerResult);
    RUN(TestDrift);
    RUN(TestRowsSaveAndLoad);
    RUN(TestRowsMigration);
    RUN(TestRowsMalformed);
    RUN(TestOffersDraw);
    RUN(TestOffersRealRoster);
    RUN(TestOfferSets);
    RUN(TestOfferRows);
    RUN(TestOffersMigration);
    RUN(TestDuelRows);
    RUN(TestDuelLogAndSql);
    RUN(TestDuelMigration);
    RUN(TestQueueMigration);
    RUN(TestWorldMigration);
    RUN(TestWorldBuild);
    RUN(TestRosterRisks);
    RUN(TestPlayResults);
    RUN(TestPlayTexts);
    RUN(TestPlayWalk);
    RUN(TestChairTextsMigration);
    RUN(TestDuelTextsMigration);
    RUN(TestMilestones);
    RUN(TestBreadcrumbMigration);
    RUN(TestBreadcrumbSplit);
    RUN(TestRulesQuestMigration);

    std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
