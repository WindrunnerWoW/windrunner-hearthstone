// Development tool (not part of build_tests.ps1): plays mirrored AI-vs-AI games between two weight sets of the
// SAME profile, to measure whether an AI change helps. Reuses the deck builders and game loop of match_ai_tests.cpp.
//
//   cl /std:c++17 /O2 /EHsc /DHS_AI_TUNING ai_ab.cpp <match sources>      (see tests/ai_ab.ps1)
//   ai_ab.exe <games> <hard|normal> [A:key=value ...] [B:key=value ...]
//
// A and B start from the default Weights; the keys below override them per side. The result is B's win rate.

#include "../src/match/HsMatch.h"
#include "../src/match/HsMatchAi.h"
#include "../src/match/HsMatchAiInternal.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace Hs
{
namespace Match
{
    namespace
    {
        AiDetail::Weights g_w[2];
        AiProfile g_profile = AiProfile::Hard;
    }

    Action AbChoose(Match const& m, int side, CardDb const& db, int actions, AiProfile prof, uint32_t seed, AiDecisionInfo* info)
    {
        AiDetail::g_tuneWeights = &g_w[side];
        return ChooseAiAction(m, side, db, actions, prof, seed, info);
    }
    Action AbChoose(Match const& m, int side, CardDb const& db, int actions, AiProfile prof, uint32_t seed)
    {
        return AbChoose(m, side, db, actions, prof, seed, nullptr);
    }
    Action AbChoose(MatchView const& v, CardDb const& db, int actions)
    {
        return ChooseAiAction(v, db, actions);
    }
}
}

#define ChooseAiAction AbChoose
#define main match_ai_main
#include "match_ai_tests.cpp"
#undef main
#undef ChooseAiAction

namespace
{
    bool Set(Hs::Match::AiDetail::Weights& w, std::string const& key, double v)
    {
        using namespace Hs::Match::AiDetail;
#define INT_KEY(k) if (key == #k) { w.k = static_cast<int>(v); return true; }
#define DBL_KEY(k) if (key == #k) { w.k = v; return true; }
        INT_KEY(width) INT_KEY(replyK) INT_KEY(replyBudget) INT_KEY(lethalWidth) INT_KEY(classBurst) INT_KEY(resample)
        INT_KEY(resampleN) INT_KEY(normalWidth) INT_KEY(normalDepth) INT_KEY(normalBudget) INT_KEY(normalKeep)
        INT_KEY(mulliganMode)
        DBL_KEY(normalNoise) DBL_KEY(threatW) DBL_KEY(pressureW) DBL_KEY(manaWaste) DBL_KEY(unknownCard)
        DBL_KEY(myHero) DBL_KEY(enemyMinion) DBL_KEY(presence)
#undef INT_KEY
#undef DBL_KEY
        return false;
    }
}

int main(int argc, char** argv)
{
    using namespace Hs::Match;
    int const games = argc > 1 ? std::atoi(argv[1]) : 200;
    AiProfile const prof = (argc > 2 && std::strcmp(argv[2], "normal") == 0) ? AiProfile::Normal : AiProfile::Hard;
    for (int i = 3; i < argc; ++i)
    {
        std::string const a = argv[i];
        std::size_t const eq = a.find('=');
        if (a.size() < 4 || a[1] != ':' || eq == std::string::npos || (a[0] != 'A' && a[0] != 'B'))
        {
            std::printf("bad argument %s\n", a.c_str());
            return 2;
        }
        if (!Set(g_w[a[0] == 'A' ? 0 : 1], a.substr(2, eq - 2), std::atof(a.c_str() + eq + 1)))
        {
            std::printf("unknown key %s\n", a.c_str());
            return 2;
        }
    }
    int bWins = 0, aWins = 0, draws = 0;
    Stats st;
    for (int g = 0; g < games; ++g)
    {
        Pairing const p = MakePairing(5000 + g);
        uint32_t const seed = 91000u + static_cast<uint32_t>(g);
        // A is side 0 in game 1 and side 1 in game 2 (the weights follow the side through AbChoose via g_w[side]).
        for (int k = 0; k < 2; ++k)
        {
            // side 0 uses g_w[0], side 1 uses g_w[1]: swap the sets for the second game so B plays both seats.
            if (k == 1)
                std::swap(g_w[0], g_w[1]);
            GameResult const r = PlayGame(p.deck0, p.cls0, p.deck1, p.cls1, seed + 500u * static_cast<uint32_t>(k), 0, prof, prof, st);
            int const bSide = k == 0 ? 1 : 0; // after the swap g_w[0] holds B's weights in game 2
            if (r.winner < 0)
                ++draws;
            else if (r.winner == bSide)
                ++bWins;
            else
                ++aWins;
            if (k == 1)
                std::swap(g_w[0], g_w[1]);
        }
    }
    int const total = aWins + bWins + draws;
    std::printf("%s A vs B, %d games: A %d, B %d, draws %d: B win rate %.1f%% (refusals %d, avg sims %.0f)\n",
                AiProfileCode(prof), total, aWins, bWins, draws, 100.0 * (bWins + 0.5 * draws) / std::max(1, total),
                st.refusals, st.hardCalls ? static_cast<double>(st.hardSims) / static_cast<double>(st.hardCalls) : 0.0);
    return 0;
}
