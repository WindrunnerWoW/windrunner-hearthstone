// Battlegrounds NPC simulation: thousands of seeded 8-player games played by the real NPC
// policy (src/battlegrounds/HsBgAi), with a rule audit, a determinism check, a comparison against the placeholder
// policy and a report (game length, tribe results). Balance is reported, not tuned.
//   bg_sim.exe [games] [report.md] [hero games]     default 2000 games, 400 hero-pick games; exit code 0 = no crash and
//   no rule violation. Each hero gets about hero games * 8 / 86 player-games, so tuning a hero wants 2000 or more.
//
// Rule violations (any of them fails the run): a game that does not finish or times out, a pool that does not add
// up (Shop::CheckConservation), a board over 7 / hand over 10 / negative gold / tier outside 1..6, a fight between two
// players of the same faction, the damage cap broken, two runs of one seed that differ, and a policy that has more than
// 2% of its actions refused (a refusal changes nothing, but it means the policy asked for something the rules forbid).

#include "../src/battlegrounds/HsBgAi.h"
#include "../src/battlegrounds/HsBgGame.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

using namespace Hs::Bg;

static int g_checks = 0;
static int g_failed = 0;
static std::FILE* g_report = nullptr;

#define CHECK(cond)                                                                                   \
    do {                                                                                              \
        ++g_checks;                                                                                   \
        if (!(cond)) {                                                                                \
            ++g_failed;                                                                               \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                               \
        }                                                                                             \
    } while (0)

namespace
{
    struct TribeName
    {
        uint32_t bit;
        char const* name;
    };
    constexpr TribeName kTribes[] = { { kTribeBeast, "Beast" }, { kTribeDemon, "Demon" }, { kTribeDragon, "Dragon" },
        { kTribeElemental, "Elemental" }, { kTribeMech, "Mech" }, { kTribeMurloc, "Murloc" },
        { kTribeNaga, "Naga" }, { kTribePirate, "Pirate" }, { kTribeQuilboar, "Quilboar" } };
    constexpr int kTribeCount = 9;

    std::string SetText(uint32_t m)
    {
        std::string s;
        for (int t = 0; t < kTribeCount; ++t)
            if (m & kTribes[t].bit)
                s += (s.empty() ? "" : "+") + std::string(kTribes[t].name);
        return s;
    }

    // What one player looked like at the end of its last shop phase.
    struct Snapshot
    {
        bool valid = false;
        int tier = 1;
        int boardSize = 0;
        int dominant = -1;     // index into kTribes, -1 = mixed
        int turns = 0;
    };

    // Wraps the policy under test: audits the invariants after every turn and records the final board of each player.
    class SimPolicy : public NpcPolicy
    {
    public:
        explicit SimPolicy(NpcPolicy* inner) : m_inner(inner) {}

        void PlayTurn(Game& g, int player) override
        {
            if (g.Round() == 1 && player == 0)
                for (Snapshot& s : snapshots)
                    s = Snapshot();
            m_inner->PlayTurn(g, player);
            PlayerShop const& ps = g.GetShop().State(player);
            if (ps.board.Size() > kBoardMax || ps.hand.Size() > kHandMax || ps.gold < 0 || ps.tier < kMinTier || ps.tier > kMaxTier)
                ++violations;
            Snapshot& s = snapshots[player];
            s.valid = true;
            s.tier = ps.tier;
            s.boardSize = ps.board.Size();
            ++s.turns;
            int counts[kTribeCount] = { 0 };
            for (Minion const& m : ps.board.Items())
                for (int t = 0; t < kTribeCount; ++t)
                    if ((m.Tribes() & ~static_cast<uint32_t>(kTribeAll) & kTribes[t].bit) != 0)
                        ++counts[t];
            int best = 0;
            for (int t = 1; t < kTribeCount; ++t)
                if (counts[t] > counts[best])
                    best = t;
            s.dominant = counts[best] >= 3 ? best : -1;
        }

        Snapshot snapshots[kPlayers];
        int violations = 0;

    private:
        NpcPolicy* m_inner;
    };

    // Players of faction A use `a`, players of faction B use `b`.
    class SplitPolicy : public NpcPolicy
    {
    public:
        SplitPolicy(NpcPolicy* a, NpcPolicy* b) : m_a(a), m_b(b) {}
        void PlayTurn(Game& g, int player) override { (FactionOf(player) == 0 ? m_a : m_b)->PlayTurn(g, player); }
    private:
        NpcPolicy* m_a;
        NpcPolicy* m_b;
    };

    struct TribeResult
    {
        int players = 0;
        int placementSum = 0;
        int firstPlace = 0;
        int factionWins = 0;
        int tierSum = 0;
    };

    void Print(char const* line)
    {
        std::printf("%s\n", line);
        if (g_report)
            std::fprintf(g_report, "%s\n", line);
    }

    void Printf(char const* fmt, int a = 0, int b = 0, int c = 0, int d = 0, int e = 0, int f = 0, int g = 0)
    {
        char buf[512];
        std::snprintf(buf, sizeof(buf), fmt, a, b, c, d, e, f, g);
        Print(buf);
    }

    double Pct(int n, int d) { return d == 0 ? 0.0 : 100.0 * n / d; }
}

int main(int argc, char** argv)
{
    int games = 2000;
    if (argc > 1 && std::atoi(argv[1]) > 0)
        games = std::atoi(argv[1]);
    int heroGamesWanted = std::min(games, 400);
    if (argc > 3 && std::atoi(argv[3]) > 0)
        heroGamesWanted = std::atoi(argv[3]);
    if (argc > 2)
    {
#ifdef _MSC_VER
        if (fopen_s(&g_report, argv[2], "w") != 0)
            g_report = nullptr;
#else
        g_report = std::fopen(argv[2], "w");
#endif
    }

    Ai ai;
    SimPolicy policy(&ai);

    int finished = 0, timedOut = 0, totalFights = 0, sameFaction = 0, capBroken = 0, poolBroken = 0;
    int winsByFaction[2] = { 0, 0 };
    long long totalRounds = 0;
    std::map<int, int> roundHistogram;      // by 5-round bucket
    std::vector<int> rounds;
    TribeResult byTribe[kTribeCount + 1];   // the last entry is "mixed"
    long long tierAtEnd[2] = { 0, 0 };      // winners / losers of the faction fight
    int tierCount[2] = { 0, 0 };
    int maxTierReached = 0;
    int totalDamage = 0;
    // How often each pair of faction tribe sets came up and who won.
    auto Mask = [](std::vector<uint32_t> const& tribes)
    {
        uint32_t m = 0;
        for (uint32_t t : tribes)
            m |= t;
        return m;
    };
    std::map<std::pair<uint32_t, uint32_t>, int> setGames;
    std::map<std::pair<uint32_t, uint32_t>, int> setWinsA;

    for (int i = 0; i < games; ++i)
    {
        GameSetup setup;
        setup.seed = static_cast<uint32_t>(i + 1);
        setup.policy = &policy;
        Game g(setup);
        if (!g.Over() || (g.WinnerFaction() != 0 && g.WinnerFaction() != 1))
        {
            CHECK(false);
            continue;
        }
        ++finished;
        if (g.TimedOut())
            ++timedOut;
        ++winsByFaction[g.WinnerFaction()];
        {
            std::pair<uint32_t, uint32_t> const key(Mask(g.FactionTribes(0)), Mask(g.FactionTribes(1)));
            ++setGames[key];
            if (g.WinnerFaction() == 0)
                ++setWinsA[key];
        }
        totalRounds += g.Round();
        rounds.push_back(g.Round());
        ++roundHistogram[g.Round() / 5 * 5];
        if (!g.GetShop().CheckConservation())
            ++poolBroken;
        for (CombatRecord const& c : g.Combats())
        {
            ++totalFights;
            if (FactionOf(c.a) == FactionOf(c.b))
                ++sameFaction;
            if (c.capped && c.damage > 15)
                ++capBroken;
            totalDamage += c.damage;
        }
        for (int p = 0; p < kPlayers; ++p)
        {
            Snapshot const& s = policy.snapshots[p];
            if (!s.valid)
                continue;
            TribeResult& r = byTribe[s.dominant < 0 ? kTribeCount : s.dominant];
            ++r.players;
            r.placementSum += g.Placement(p);
            r.tierSum += s.tier;
            if (g.Placement(p) == 1)
                ++r.firstPlace;
            if (FactionOf(p) == g.WinnerFaction())
                ++r.factionWins;
            int const side = FactionOf(p) == g.WinnerFaction() ? 0 : 1;
            tierAtEnd[side] += s.tier;
            ++tierCount[side];
            maxTierReached = std::max(maxTierReached, s.tier);
        }
    }

    std::sort(rounds.begin(), rounds.end());
    int const median = rounds.empty() ? 0 : rounds[rounds.size() / 2];
    int const p10 = rounds.empty() ? 0 : rounds[rounds.size() / 10];
    int const p90 = rounds.empty() ? 0 : rounds[rounds.size() * 9 / 10];

    Print("# Battlegrounds simulation (HsBgAi)");
    Print("");
    Printf("%d games, seeds 1..%d, real effects (%d entries), 5 of the playable tribes per lobby, 40 health, no armor.", games, games,
        EffectRegistry::Global().Size());
    Print("");
    Print("## Game length");
    Printf("- finished %d of %d, timed out %d", finished, games, timedOut);
    Printf("- rounds: average %d, median %d, 10th percentile %d, 90th percentile %d", static_cast<int>(totalRounds / std::max(finished, 1)), median, p10, p90);
    Printf("- longest %d, shortest %d", rounds.empty() ? 0 : rounds.back(), rounds.empty() ? 0 : rounds.front());
    for (auto const& kv : roundHistogram)
        Printf("  - rounds %d-%d: %d games", kv.first, kv.first + 4, kv.second);
    Printf("- fights %d, average damage per fight %d", totalFights, totalFights == 0 ? 0 : totalDamage / totalFights);
    Printf("- wins by faction: A %d, B %d", winsByFaction[0], winsByFaction[1]);
    Printf("- average final tavern tier: winning faction %d.%d, losing faction %d.%d",
        tierCount[0] ? static_cast<int>(tierAtEnd[0] * 10 / tierCount[0]) / 10 : 0, tierCount[0] ? static_cast<int>(tierAtEnd[0] * 10 / tierCount[0]) % 10 : 0,
        tierCount[1] ? static_cast<int>(tierAtEnd[1] * 10 / tierCount[1]) / 10 : 0, tierCount[1] ? static_cast<int>(tierAtEnd[1] * 10 / tierCount[1]) % 10 : 0);
    Print("");
    // The faction tribe sets, most common first (a weak roll shows up here).
    std::vector<std::pair<std::pair<uint32_t, uint32_t>, int>> sets(setGames.begin(), setGames.end());
    std::sort(sets.begin(), sets.end(), [](auto const& a, auto const& b) { return a.second > b.second; });
    Print("## Faction tribe sets (this section runs one shared set per game)");
    Print("| A set | B set | Games | A won |");
    Print("| --- | --- | --- | --- |");
    for (std::size_t i = 0; i < sets.size() && i < 12; ++i)
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "| %s | %s | %d | %d |", SetText(sets[i].first.first).c_str(),
            SetText(sets[i].first.second).c_str(), sets[i].second, setWinsA[sets[i].first]);
        Print(buf);
    }
    Print("");
    Print("## Tribe results (board of 3 or more minions of one tribe at the last shop phase)");
    Print("");
    Print("| Tribe | Player-games | Avg placement in team (1 best) | First in team | Team won | Avg final tier |");
    Print("| --- | --- | --- | --- | --- | --- |");
    for (int t = 0; t <= kTribeCount; ++t)
    {
        TribeResult const& r = byTribe[t];
        char buf[256];
        std::snprintf(buf, sizeof(buf), "| %s | %d | %.2f | %.1f%% | %.1f%% | %.1f |", t < kTribeCount ? kTribes[t].name : "Mixed",
            r.players, r.players ? static_cast<double>(r.placementSum) / r.players : 0.0, Pct(r.firstPlace, r.players),
            Pct(r.factionWins, r.players), r.players ? static_cast<double>(r.tierSum) / r.players : 0.0);
        Print(buf);
    }
    Print("");

    AiStats const& st = ai.Stats();
    Print("## Policy actions");
    Printf("- NPC turns %d, actions %d (buys %d, sells %d, upgrades %d, refreshes %d)", st.turns, st.actions, st.buys, st.sells, st.upgrades, st.refreshes);
    Printf("- freezes %d, triples completed %d, refused actions %d", st.freezes, st.triples, st.refusedTotal());
    Printf("- sends %d, of which %d completed a triple for the ally (%d%%)", st.sends, st.sendTriples,
        st.sends == 0 ? 0 : 100 * st.sendTriples / st.sends);
    for (int r = 0; r < 16; ++r)
        if (st.refused[r] > 0)
            Printf("  - refused with %d: %d", r, st.refused[r]);
    Print("");

    // The invariants.
    CHECK(finished == games);
    CHECK(timedOut == 0);
    CHECK(poolBroken == 0);
    CHECK(policy.violations == 0);
    CHECK(sameFaction == 0);
    CHECK(capBroken == 0);
    CHECK(st.refusedTotal() * 50 <= st.actions);   // at most 2% refused

    // Determinism: the same seed gives the same game, twice.
    int mismatches = 0;
    int const detGames = std::min(games, 25);
    for (int i = 0; i < detGames; ++i)
    {
        Ai a1, a2;
        GameSetup s1;
        s1.seed = static_cast<uint32_t>(i + 1);
        s1.policy = &a1;
        GameSetup s2 = s1;
        s2.policy = &a2;
        Game g1(s1);
        Game g2(s2);
        if (!(g1.Combats() == g2.Combats()) || g1.EliminationOrder() != g2.EliminationOrder() || g1.Round() != g2.Round())
            ++mismatches;
    }
    CHECK(mismatches == 0);
    Print("## Determinism");
    Printf("- %d seeds run twice with fresh policies: %d mismatches", detGames, mismatches);
    Print("");

    // The real policy against the placeholder, sides swapped every other game.
    int const duels = std::min(games, 400);
    int aiWins = 0, aiRoundsSum = 0;
    for (int i = 0; i < duels; ++i)
    {
        Ai real;
        SimpleNpcPolicy simple;
        bool const aiIsA = (i % 2) == 0;
        SplitPolicy split(aiIsA ? static_cast<NpcPolicy*>(&real) : &simple, aiIsA ? static_cast<NpcPolicy*>(&simple) : &real);
        GameSetup setup;
        setup.seed = static_cast<uint32_t>(10000 + i);
        setup.policy = &split;
        Game g(setup);
        CHECK(g.Over());
        if (g.WinnerFaction() == (aiIsA ? 0 : 1))
            ++aiWins;
        aiRoundsSum += g.Round();
    }
    CHECK(aiWins * 100 > duels * 55);   // the real policy must beat the placeholder clearly
    Print("## Real policy against the placeholder");
    Printf("- %d games, sides swapped every other game: HsBgAi wins %d (%d%%), average %d rounds", duels, aiWins,
        duels ? aiWins * 100 / duels : 0, duels ? aiRoundsSum / duels : 0);
    Print("");

    // Faction pools, a hero pick each, the real policy using hero powers and sending minions.
    Ai ai15c;
    SimPolicy policy15c(&ai15c);
    int const games15c = heroGamesWanted;
    int broken15c = 0;
    int heroGames = 0;
    std::map<int, TribeResult> byHero;
    std::map<std::pair<uint32_t, uint32_t>, int> setGames15c;
    std::map<std::pair<uint32_t, uint32_t>, int> setWinsA15c;
    for (int i = 0; i < games15c; ++i)
    {
        GameSetup setup;
        setup.seed = static_cast<uint32_t>(20000 + i);
        setup.policy = &policy15c;
        setup.heroPick = true;
        setup.factionPools = true;
        Game g(setup);
        CHECK(g.Over());
        if (!g.GetShop().CheckConservation())
            ++broken15c;
        {
            std::pair<uint32_t, uint32_t> const key(Mask(g.FactionTribes(0)), Mask(g.FactionTribes(1)));
            ++setGames15c[key];
            if (g.WinnerFaction() == 0)
                ++setWinsA15c[key];
        }
        for (int p = 0; p < kPlayers; ++p)
        {
            int const hero = g.PlayerHero(p);
            if (hero < 0)
                continue;
            ++heroGames;
            TribeResult& r = byHero[hero];
            ++r.players;
            r.placementSum += g.Placement(p);
            r.tierSum += g.GetShop().State(p).tier;
            if (g.Placement(p) == 1)
                ++r.firstPlace;
            if (FactionOf(p) == g.WinnerFaction())
                ++r.factionWins;
        }
    }
    AiStats const& st15c = ai15c.Stats();
    Print("## Heroes, armor and sending");
    Printf("%d games, faction pools, a hero pick each, the real policy using hero powers and sending.", games15c);
    Printf("- hero powers used %d, minions sent %d, NPC turns %d, conservation failures %d", st15c.powerUses, st15c.sends, st15c.turns, broken15c);
    Print("");
    Print("| Hero | Player-games | Avg placement in team | First in team | Team won | Avg final tier |");
    Print("| --- | --- | --- | --- | --- | --- |");
    for (auto const& kv : byHero)
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "| %s | %d | %.2f | %.1f%% | %.1f%% | %.1f |", HeroAt(kv.first).id, kv.second.players,
            kv.second.players ? static_cast<double>(kv.second.placementSum) / kv.second.players : 0.0,
            Pct(kv.second.firstPlace, kv.second.players), Pct(kv.second.factionWins, kv.second.players),
            kv.second.players ? static_cast<double>(kv.second.tierSum) / kv.second.players : 0.0);
        Print(buf);
    }
    Print("");
    {
        std::vector<std::pair<std::pair<uint32_t, uint32_t>, int>> sets15c(setGames15c.begin(), setGames15c.end());
        std::sort(sets15c.begin(), sets15c.end(), [](auto const& a, auto const& b) { return a.second > b.second; });
        Print("## Faction tribe sets (each faction rolls its own 5)");
        Print("| A set | B set | Games | A won |");
        Print("| --- | --- | --- | --- |");
        for (std::size_t i = 0; i < sets15c.size() && i < 12; ++i)
        {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "| %s | %s | %d | %d |", SetText(sets15c[i].first.first).c_str(),
                SetText(sets15c[i].first.second).c_str(), sets15c[i].second, setWinsA15c[sets15c[i].first]);
            Print(buf);
        }
        Print("");
    }
    CHECK(broken15c == 0);
    CHECK(policy15c.violations == 0);
    CHECK(heroGames == games15c * kPlayers);
    CHECK(st15c.powerUses > 0 && st15c.sends > 0);
    CHECK(st15c.refusedTotal() * 50 <= st15c.actions);

    // Quests and rewards.
    Ai ai15d;
    SimPolicy policy15d(&ai15d);
    int const games15d = std::min(games, 400);
    int broken15d = 0;
    std::map<int, int> questOffered, questCompletions, questDoneSum, rewardActive;
    for (int i = 0; i < games15d; ++i)
    {
        GameSetup setup;
        setup.seed = static_cast<uint32_t>(30000 + i);
        setup.policy = &policy15d;
        setup.heroPick = true;
        setup.factionPools = true;
        setup.quests = true;
        Game g(setup);
        CHECK(g.Over());
        if (!g.GetShop().CheckConservation())
            ++broken15d;
        for (int p = 0; p < kPlayers; ++p)
        {
            int const q = g.PlayerQuest(p);
            if (q < 0)
                continue;
            ++questOffered[q];
            RewardState const& rs = g.GetShop().State(p).rewardState;
            if (g.QuestDone(p))
            {
                ++questCompletions[q];
                questDoneSum[q] += rs.doneTurn;
            }
            if (rs.active)
                ++rewardActive[rs.id];
        }
    }
    AiStats const& st15d = ai15d.Stats();
    Print("## Quests and rewards");
    Printf("%d games, quests on (turn 4), the real policy taking a pair.", games15d);
    Printf("- NPC turns %d, conservation failures %d, refused %d of %d actions", st15d.turns, broken15d, st15d.refusedTotal(), st15d.actions);
    Print("");
    Print("| Quest | Offered | Completed | Avg completion round |");
    Print("| --- | --- | --- | --- |");
    for (auto const& kv : questOffered)
    {
        int const done = questCompletions.count(kv.first) ? questCompletions[kv.first] : 0;
        double const avg = done ? static_cast<double>(questDoneSum[kv.first]) / done : 0.0;
        char buf[256];
        std::snprintf(buf, sizeof(buf), "| %s | %d | %.1f%% | %.1f |", QuestAt(kv.first).id, kv.second, Pct(done, kv.second), avg);
        Print(buf);
    }
    Print("");
    Print("| Reward | Active at game end |");
    Print("| --- | --- |");
    for (auto const& kv : rewardActive)
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "| %s | %d |", RewardAt(kv.first).id, kv.second);
        Print(buf);
    }
    Print("");
    CHECK(broken15d == 0);
    CHECK(policy15d.violations == 0);
    CHECK(st15d.refusedTotal() * 50 <= st15d.actions);

    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    if (g_report)
        std::fclose(g_report);
    return g_failed == 0 ? 0 : 1;
}
