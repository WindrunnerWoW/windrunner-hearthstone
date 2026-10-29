// Battlegrounds policy A/B: the shipped policy (A) against a tuned one (B), sides swapped every other game, so a
// faction-wide edge cancels. Reports B's team win rate with a 95% margin and the average rounds.
//   bg_ab.exe [games] [key=value ...]    keys: tribeBonus tierShift refreshExtra lockHeld lockRound biasSpread swapMargin cheapUpgrade
//   tierWeight seed
//   Example: bg_ab.exe 4000 tribeBonus=10 lockRound=2

#include "../src/battlegrounds/HsBgAi.h"
#include "../src/battlegrounds/HsBgGame.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace Hs::Bg;

namespace
{
    class SplitPolicy : public NpcPolicy
    {
    public:
        SplitPolicy(NpcPolicy* a, NpcPolicy* b) : m_a(a), m_b(b) {}
        void PlayTurn(Game& g, int player) override { (FactionOf(player) == 0 ? m_a : m_b)->PlayTurn(g, player); }
    private:
        NpcPolicy* m_a;
        NpcPolicy* m_b;
    };

    int g_seedBase = 50000;   // seed=N moves the games to other seeds (a check against tuning to one set)

    bool Apply(AiTuning& t, std::string const& kv)
    {
        std::size_t const eq = kv.find('=');
        if (eq == std::string::npos)
            return false;
        std::string const k = kv.substr(0, eq);
        int const v = std::atoi(kv.c_str() + eq + 1);
        if (k == "tribeBonus") t.tribeBonus = v;
        else if (k == "tierShift") t.tierShift = v;
        else if (k == "refreshExtra") t.refreshExtra = v;
        else if (k == "lockHeld") t.lockHeld = v;
        else if (k == "lockRound") t.lockRound = v;
        else if (k == "tribeVariety") t.tribeVariety = v;
        else if (k == "seed") g_seedBase = v;
        else if (k == "biasSpread") t.biasSpread = v;
        else if (k == "swapMargin") t.swapMargin = v;
        else if (k == "cheapUpgrade") t.cheapUpgrade = v;
        else if (k == "tierWeight") t.tierWeight = v;
        else return false;
        return true;
    }
}

int main(int argc, char** argv)
{
    int games = 2000;
    if (argc > 1 && std::atoi(argv[1]) > 0)
        games = std::atoi(argv[1]);
    AiTuning tune;
    for (int i = 2; i < argc; ++i)
        if (!Apply(tune, argv[i]))
        {
            std::printf("unknown setting: %s\n", argv[i]);
            return 2;
        }
    int bWins = 0;
    long long rounds = 0;
    long long distinctA = 0, distinctB = 0;   // different favourite tribes among the four players of a team
    auto distinct = [](Ai const& ai, int faction)
    {
        uint32_t seen[kPlayers];
        int n = 0;
        for (int p = 0; p < kPlayers; ++p)
        {
            if (FactionOf(p) != faction)
                continue;
            bool known = false;
            for (int i = 0; i < n; ++i)
                known = known || seen[i] == ai.Favourite(p);
            if (!known)
                seen[n++] = ai.Favourite(p);
        }
        return n;
    };
    for (int i = 0; i < games; ++i)
    {
        Ai a, b;
        b.SetTuning(tune);
        bool const bIsA = (i % 2) == 0;
        SplitPolicy split(bIsA ? static_cast<NpcPolicy*>(&b) : &a, bIsA ? static_cast<NpcPolicy*>(&a) : &b);
        GameSetup setup;
        setup.seed = static_cast<uint32_t>(g_seedBase + i);
        setup.policy = &split;
        Game g(setup);
        if (g.WinnerFaction() == (bIsA ? 0 : 1))
            ++bWins;
        rounds += g.Round();
        distinctA += distinct(a, bIsA ? 1 : 0);
        distinctB += distinct(b, bIsA ? 0 : 1);
    }
    double const p = static_cast<double>(bWins) / games;
    double const margin = 1.96 * std::sqrt(p * (1 - p) / games);
    std::printf("B wins %d of %d = %.1f%% +/- %.1f, average %.1f rounds\n", bWins, games, p * 100, margin * 100,
        static_cast<double>(rounds) / games);
    std::printf("different favourite tribes per team of four: A %.2f, B %.2f\n",
        static_cast<double>(distinctA) / games, static_cast<double>(distinctB) / games);
    return 0;
}
