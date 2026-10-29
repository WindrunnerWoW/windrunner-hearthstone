// Standalone tests of the headless game runner (src/ladder/HsLadderSim.*) and a benchmark of AI-versus-AI
// games with the real library decks (data/sql/world/0002_hearthstone_opponents.sql) and every card of the dataset
// (tests/match_all_cards.h).
//   ladder_sim_tests.exe [directory [--bench games [threads]]]
// Without --bench a short benchmark runs (a few games per pairing). With --bench, `games` games per pairing are
// played and timed, on `threads` worker threads (default 1) in addition to the serial run. Exit code 0 = all checks
// passed.

#include "../src/ladder/HsLadderSim.h"
#include "match_all_cards.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace Hs;
using namespace Hs::Ladder;
using Hs::Match::AiProfile;

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
    using Clock = std::chrono::steady_clock;

    Hs::Match::CardDb const& Db() { return MatchAllCards::Db(); }

    std::string g_dir = ".";
    int g_benchGames = 0;   // 0: the short default benchmark
    int g_benchThreads = 1;

    // ---- the library decks of world migration 0002 ----------------------------------------------------

    struct LibDeck
    {
        std::string key, name, cls, kind, finder;
        std::vector<std::string> ids; // one entry per copy
    };

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

    // The rows of "INSERT INTO `table` (...) VALUES ('a', 'b', ...), (...);" with plain quoted string cells (the
    // deck rows have no NULLs or expressions).
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

    std::vector<LibDeck> const& Library()
    {
        static std::vector<LibDeck> decks = []() {
            std::vector<LibDeck> out;
            std::string sql;
            if (!ReadFile(g_dir + "/../data/sql/world/0002_hearthstone_opponents.sql", sql))
                return out;
            std::vector<std::vector<std::string>> rows;
            if (!ParseStringRows(sql, "hearthstone_deck_template", rows))
                return out;
            for (std::vector<std::string> const& r : rows)
            {
                if (r.size() != 6)
                    continue;
                LibDeck d;
                d.key = r[0];
                d.name = r[1];
                d.cls = r[2];
                d.kind = r[3];
                d.finder = r[4];
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
                        d.ids.push_back(id);
                }
                out.push_back(d);
            }
            return out;
        }();
        return decks;
    }

    LibDeck const* FindDeck(std::string const& key)
    {
        for (LibDeck const& d : Library())
            if (d.key == key)
                return &d;
        return nullptr;
    }

    SimEntrant Entrant(LibDeck const& d, AiProfile profile)
    {
        SimEntrant e;
        e.name = d.name;
        e.heroClass = d.cls;
        e.deck = d.ids;
        e.profile = profile;
        return e;
    }

    SimEntrant Entrant(char const* key, AiProfile profile)
    {
        LibDeck const* d = FindDeck(key);
        return d ? Entrant(*d, profile) : SimEntrant();
    }

    bool SameResult(SimResult const& x, SimResult const& y)
    {
        return x.outcome == y.outcome && x.reason == y.reason && x.firstSide == y.firstSide && x.turns == y.turns &&
               x.actions == y.actions && x.decisions[0] == y.decisions[0] && x.decisions[1] == y.decisions[1] &&
               x.simulations[0] == y.simulations[0] && x.simulations[1] == y.simulations[1] &&
               x.heroHealth[0] == y.heroHealth[0] && x.heroHealth[1] == y.heroHealth[1] && x.stateHash == y.stateHash &&
               x.error == y.error;
    }

    Hs::Match::Action Simple(Hs::Match::ActionType t)
    {
        Hs::Match::Action a;
        a.type = t;
        return a;
    }

    // ---- tests ----------------------------------------------------------------------------------------

    // Every library deck of 0002 is a 30 card deck the engine accepts.
    void TestLibraryDecks()
    {
        std::vector<LibDeck> const& lib = Library();
        std::printf("    %d library decks read\n", static_cast<int>(lib.size()));
        CHECK(lib.size() == 281);   // 221 + the 60 Naxx-era ladder decks (25356b8)
        int refused = 0;
        for (LibDeck const& d : lib)
        {
            CHECK(d.ids.size() == 30);
            for (std::string const& id : d.ids)
                if (!Db().Supported(id))
                {
                    std::printf("    deck %s: %s is not supported\n", d.key.c_str(), id.c_str());
                    ++refused;
                }
        }
        CHECK(refused == 0);
    }

    void TestFinishesWithRealResult()
    {
        SimResult const r = SimulateGame(Db(), Entrant("normal_mage", AiProfile::Normal),
            Entrant("normal_warrior", AiProfile::Normal), 1234u);
        CHECK(r.outcome != SimOutcome::Failed);
        CHECK(r.error.empty());
        CHECK(r.firstSide == 0 || r.firstSide == 1);
        CHECK(r.turns > 0 && r.turns <= Hs::Match::kMaxTurns);
        CHECK(r.decisions[0] > 0 && r.decisions[1] > 0);
        CHECK(r.actions == r.decisions[0] + r.decisions[1]); // every decision was applied
        CHECK(r.stateHash != 0);
        if (r.outcome == SimOutcome::Win0)
            CHECK(r.heroHealth[1] <= 0 || r.reason == Hs::Match::EndReason::Concede);
        if (r.outcome == SimOutcome::Win1)
            CHECK(r.heroHealth[0] <= 0 || r.reason == Hs::Match::EndReason::Concede);
        // The real AI never concedes.
        CHECK(r.reason != Hs::Match::EndReason::Concede);
    }

    // The same inputs give the same game; the seed and the first side change it.
    void TestDeterminism()
    {
        SimEntrant const a = Entrant("hard_hunter", AiProfile::Hard);
        SimEntrant const b = Entrant("hard_priest", AiProfile::Normal);
        SimResult const r1 = SimulateGame(Db(), a, b, 77u);
        SimResult const r2 = SimulateGame(Db(), a, b, 77u);
        CHECK(SameResult(r1, r2));
        CHECK(r1.outcome != SimOutcome::Failed);
        int differ = 0;
        for (uint32_t s = 78; s < 84; ++s)
            differ += SimulateGame(Db(), a, b, s).stateHash != r1.stateHash ? 1 : 0;
        CHECK(differ >= 5);
        // The first side can be forced either way, and each forced game repeats exactly too.
        for (int first = 0; first < 2; ++first)
        {
            SimResult const f1 = SimulateGame(Db(), a, b, 90u, first);
            SimResult const f2 = SimulateGame(Db(), a, b, 90u, first);
            CHECK(f1.firstSide == first);
            CHECK(SameResult(f1, f2));
        }
        // The AI seeds of the two sides never coincide and depend on every input.
        CHECK(SimAiSeed(1u, 0, 5u) != SimAiSeed(1u, 1, 5u));
        CHECK(SimAiSeed(1u, 0, 5u) != SimAiSeed(2u, 0, 5u));
        CHECK(SimAiSeed(1u, 0, 5u) != SimAiSeed(1u, 0, 6u));
    }

    // A refused setup, a refused action, a wrong mulligan answer and the action bound are failures, never results.
    void TestFailures()
    {
        SimEntrant const a = Entrant("normal_mage", AiProfile::Normal);
        SimEntrant bad = Entrant("normal_warrior", AiProfile::Normal);
        bad.heroClass = "DEATHKNIGHT";
        SimResult r = SimulateGame(Db(), a, bad, 1u);
        CHECK(r.outcome == SimOutcome::Failed && r.error.find("setup refused") == 0 && r.firstSide == -1);

        SimEntrant unknown = Entrant("normal_warrior", AiProfile::Normal);
        unknown.deck[0] = "NOT_A_CARD";
        r = SimulateGame(Db(), a, unknown, 1u);
        CHECK(r.outcome == SimOutcome::Failed && r.error.find("setup refused") == 0);

        r = SimulateGame(Db(), a, a, 1u, 2);
        CHECK(r.outcome == SimOutcome::Failed && r.error == "bad first side");

        // An action of a hand card that does not exist: refused by the engine -> failed (not a concession).
        SimOptions illegal;
        illegal.chooser = [](Hs::Match::Match const& m, int side, int) {
            if (m.MulliganPending(side))
                return Simple(Hs::Match::ActionType::Mulligan);
            Hs::Match::Action play = Simple(Hs::Match::ActionType::Play);
            play.source = 99999;
            return play;
        };
        r = SimulateGame(Db(), a, a, 3u, 0, illegal);
        CHECK(r.outcome == SimOutcome::Failed);
        CHECK(r.error.find("side 0 play refused (source)") == 0);
        CHECK(r.reason == Hs::Match::EndReason::None);

        // A mulligan answered with something else.
        SimOptions noMulligan;
        noMulligan.chooser = [](Hs::Match::Match const&, int, int) { return Simple(Hs::Match::ActionType::EndTurn); };
        r = SimulateGame(Db(), a, a, 3u, 0, noMulligan);
        CHECK(r.outcome == SimOutcome::Failed && r.error == "side 0 answered the mulligan with end turn");

        // The action bound.
        SimOptions tight;
        tight.maxActions = 6;
        r = SimulateGame(Db(), a, a, 3u, 0, tight);
        CHECK(r.outcome == SimOutcome::Failed && r.error.find("action limit 6 reached") == 0 && r.actions == 6);
    }

    // A concession is the engine's real result (the runner never concedes on its own).
    void TestConcedeIsAResult()
    {
        SimOptions concede;
        concede.chooser = [](Hs::Match::Match const& m, int side, int) {
            if (m.MulliganPending(side))
                return Simple(Hs::Match::ActionType::Mulligan);
            return Simple(Hs::Match::ActionType::Concede);
        };
        SimEntrant const a = Entrant("normal_mage", AiProfile::Normal);
        SimResult const r = SimulateGame(Db(), a, a, 5u, 1, concede);
        CHECK(r.outcome == SimOutcome::Win0 && r.reason == Hs::Match::EndReason::Concede);
    }

    // Nobody ever acts: the engine's turn limit ends the game in a real draw.
    void TestTurnLimitDraw()
    {
        SimEntrant big;
        big.name = "Wall";
        big.heroClass = "WARRIOR";
        big.deck.assign(Hs::Match::kMaxDeckCards, "CS2_168"); // 60 cards: no fatigue before the turn limit
        SimOptions pass;
        pass.chooser = [](Hs::Match::Match const& m, int side, int) {
            return Simple(m.MulliganPending(side) ? Hs::Match::ActionType::Mulligan : Hs::Match::ActionType::EndTurn);
        };
        SimResult const r = SimulateGame(Db(), big, big, 9u, 0, pass);
        CHECK(r.outcome == SimOutcome::Draw);
        CHECK(r.reason == Hs::Match::EndReason::TurnLimit);
        CHECK(r.heroHealth[0] == Hs::Match::kHeroHealth && r.heroHealth[1] == Hs::Match::kHeroHealth);
        CHECK(std::strcmp(SimOutcomeCode(r.outcome), "draw") == 0);
    }

    // Games on worker threads give exactly the serial results.
    void TestThreadsMatchSerial()
    {
        std::vector<LibDeck const*> decks;
        for (LibDeck const& d : Library())
            if (d.kind == "opponent")
                decks.push_back(&d);
        CHECK(decks.size() == 18);
        if (decks.size() < 2)
            return;
        int const games = 12;
        auto play = [&](int g) {
            LibDeck const& x = *decks[static_cast<std::size_t>(g) % decks.size()];
            LibDeck const& y = *decks[static_cast<std::size_t>(g * 7 + 3) % decks.size()];
            return SimulateGame(Db(), Entrant(x, AiProfile::Normal), Entrant(y, g % 3 == 0 ? AiProfile::Hard : AiProfile::Normal),
                500u + static_cast<uint32_t>(g));
        };
        std::vector<SimResult> serial;
        for (int g = 0; g < games; ++g)
            serial.push_back(play(g));
        std::vector<SimResult> parallel(static_cast<std::size_t>(games));
        std::atomic<int> next{0};
        std::vector<std::thread> workers;
        for (int t = 0; t < 4; ++t)
            workers.emplace_back([&]() {
                for (int g = next++; g < games; g = next++)
                    parallel[static_cast<std::size_t>(g)] = play(g);
            });
        for (std::thread& w : workers)
            w.join();
        for (int g = 0; g < games; ++g)
        {
            CHECK(serial[static_cast<std::size_t>(g)].outcome != SimOutcome::Failed);
            CHECK(SameResult(serial[static_cast<std::size_t>(g)], parallel[static_cast<std::size_t>(g)]));
        }
    }

    // ---- benchmark ------------------------------------------------------------------------------------

    struct Pairing
    {
        char const* label;
        char const* finder0; // the library pool of side 0 ("normal" = easy decks, "hard")
        AiProfile p0;
        char const* finder1;
        AiProfile p1;
    };

    struct BenchStats
    {
        int games = 0, failed = 0, draws = 0, wins0 = 0;
        double ms = 0, maxMs = 0;
        long turns = 0, decisions = 0, simulations = 0;
        int maxTurns = 0;
    };

    // `games` games of a pairing: decks drawn round-robin from the two library pools (kind library or starter, by
    // finder), alternating the first side.
    BenchStats BenchPairing(Pairing const& p, int games, uint32_t base, std::vector<std::string>* failures)
    {
        std::vector<LibDeck const*> pool0, pool1;
        for (LibDeck const& d : Library())
        {
            if (d.kind != "library" && d.kind != "starter")
                continue;
            if (d.finder == p.finder0)
                pool0.push_back(&d);
            if (d.finder == p.finder1)
                pool1.push_back(&d);
        }
        BenchStats st;
        if (pool0.empty() || pool1.empty())
            return st;
        for (int g = 0; g < games; ++g)
        {
            LibDeck const& x = *pool0[(static_cast<std::size_t>(g) * 37u + base) % pool0.size()];
            LibDeck const& y = *pool1[(static_cast<std::size_t>(g) * 53u + base * 7u + 11u) % pool1.size()];
            Clock::time_point const t0 = Clock::now();
            SimResult const r = SimulateGame(Db(), Entrant(x, p.p0), Entrant(y, p.p1), base * 10007u + static_cast<uint32_t>(g), g % 2);
            double const ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
            ++st.games;
            st.ms += ms;
            st.maxMs = std::max(st.maxMs, ms);
            st.turns += r.turns;
            st.maxTurns = std::max(st.maxTurns, r.turns);
            st.decisions += r.decisions[0] + r.decisions[1];
            st.simulations += r.simulations[0] + r.simulations[1];
            if (r.outcome == SimOutcome::Failed)
            {
                ++st.failed;
                if (failures)
                    failures->push_back(x.key + " vs " + y.key + ": " + r.error);
            }
            else if (r.outcome == SimOutcome::Draw)
                ++st.draws;
            else if (r.outcome == SimOutcome::Win0)
                ++st.wins0;
        }
        return st;
    }

    Pairing const kPairings[] = {
        {"normal/easy vs normal/easy", "normal", AiProfile::Normal, "normal", AiProfile::Normal},
        {"hard/hard   vs hard/hard  ", "hard", AiProfile::Hard, "hard", AiProfile::Hard},
        {"hard/hard   vs normal/easy", "hard", AiProfile::Hard, "normal", AiProfile::Normal},
        {"normal/hard vs normal/hard", "hard", AiProfile::Normal, "hard", AiProfile::Normal},
    };

    void TestBenchmark()
    {
        int const games = g_benchGames > 0 ? g_benchGames : 6;
        std::printf("    %d games per pairing (AI profile / deck pool of each side), serial:\n", games);
        double meanMs[4] = {0, 0, 0, 0};
        int idx = 0;
        for (Pairing const& p : kPairings)
        {
            std::vector<std::string> failures;
            BenchStats const st = BenchPairing(p, games, 17u + static_cast<uint32_t>(idx), &failures);
            double const mean = st.ms / std::max(1, st.games);
            meanMs[idx++] = mean;
            std::printf("    %s: %4.0f ms/game (max %5.0f), %4.1f turns (max %d), %5.0f AI sims/game, side 0 won %d, "
                        "draws %d, failed %d\n",
                        p.label, mean, st.maxMs, static_cast<double>(st.turns) / std::max(1, st.games), st.maxTurns,
                        static_cast<double>(st.simulations) / std::max(1, st.games), st.wins0, st.draws, st.failed);
            for (std::string const& f : failures)
                std::printf("      failed: %s\n", f.c_str());
            CHECK(st.games == games);
            CHECK(st.failed == 0);
        }
        // What a ladder start costs at this pace: 20 games per NPC = 10 x roster games.
        double const mix = (meanMs[0] + meanMs[1] + meanMs[2]) / 3.0;
        std::printf("    ladder start estimate (20 games per NPC, mixed pairings, one thread): 18 NPCs %.0f s, "
                    "100 NPCs %.0f s\n", mix * 180.0 / 1000.0, mix * 1000.0 / 1000.0);

        if (g_benchThreads > 1)
        {
            std::atomic<int> next{0};
            std::atomic<int> failed{0};
            int const total = games * 4;
            Clock::time_point const t0 = Clock::now();
            std::vector<std::thread> workers;
            for (int t = 0; t < g_benchThreads; ++t)
                workers.emplace_back([&]() {
                    for (int g = next++; g < total; g = next++)
                    {
                        BenchStats const st = BenchPairing(kPairings[g % 4], 1, 900u + static_cast<uint32_t>(g), nullptr);
                        failed += st.failed;
                    }
                });
            for (std::thread& w : workers)
                w.join();
            double const s = std::chrono::duration<double>(Clock::now() - t0).count();
            std::printf("    %d threads: %d games in %.1f s = %.2f games/s\n", g_benchThreads, total, s, total / std::max(0.001, s));
            CHECK(failed == 0);
        }
    }
}

int main(int argc, char** argv)
{
    if (argc > 1)
        g_dir = argv[1];
    for (int i = 2; i < argc; ++i)
        if (std::strcmp(argv[i], "--bench") == 0 && i + 1 < argc)
        {
            g_benchGames = std::max(1, std::atoi(argv[++i]));
            if (i + 1 < argc)
                g_benchThreads = std::max(1, std::atoi(argv[++i]));
        }

    RUN(TestLibraryDecks);
    RUN(TestFinishesWithRealResult);
    RUN(TestDeterminism);
    RUN(TestFailures);
    RUN(TestConcedeIsAResult);
    RUN(TestTurnLimitDraw);
    RUN(TestThreadsMatchSerial);
    RUN(TestBenchmark);

    std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
