/*
 * Opponent AI internals shared by HsMatchAi.cpp (entry points, the view heuristic, the Normal
 * profile) and HsMatchAiSearch.cpp (the Hard profile: evaluation, search, mulligan). Not part of the
 * public API (HsMatchAi.h).
 *
 * Hidden information: everything here works on MatchView (Match::ViewFor) and on copies from
 * Match::RedactedCopy only.
 */

#ifndef MOD_HEARTHSTONE_HS_MATCH_AI_INTERNAL_H
#define MOD_HEARTHSTONE_HS_MATCH_AI_INTERNAL_H

#include "HsMatch.h"
#include "HsMatchAi.h"

#include <cstdint>

namespace Hs
{
namespace Match
{
namespace AiDetail
{
    // Small deterministic mixing / RNG (no global state, no clock).
    inline uint32_t Mix(uint32_t a, uint32_t b)
    {
        uint32_t h = a * 0x9E3779B1u + 0x7F4A7C15u;
        h ^= b + 0x85EBCA6Bu + (h << 6) + (h >> 2);
        h ^= h >> 15;
        h *= 0x2C1B3C6Du;
        h ^= h >> 12;
        h *= 0x297A2D39u;
        h ^= h >> 15;
        return h;
    }

    struct Rng
    {
        uint32_t s;
        explicit Rng(uint32_t seed) : s(seed ? seed : 0x1234567u) {}
        uint32_t Next()
        {
            s = Mix(s, 0xA511E9B3u);
            return s;
        }
        // [0, 1)
        double Unit() { return static_cast<double>(Next() >> 8) / 16777216.0; }
    };

    // Tunable constants of the Hard evaluation and search.
    struct Weights
    {
        double handMinion = 0.35;    // a minion card in hand is worth this much of the minion on the board
        double spellBase = 1.8;      // a spell card in hand: base + per cost
        double spellPerCost = 1.1;
        double secretHand = 4.5;
        double secretBoard = 5.0;
        double secretOpp = 4.2;
        double weaponHand = 0.75;
        double enemyMinion = 1.05;   // enemy minions count a bit more (they must be answered)
        double myHero = 1.15;        // own hero health counts a bit more
        double heroScale = 1.0;
        double unknownCard = 3.2;    // a card in the opponent's hand
        double threatW = 0.35;       // the opponent's damage on my face next turn
        double pressureW = 0.3;      // my damage on its face next turn
        double presence = 1.5;       // a minion on the board
        double crystal = 1.0;
        double manaWaste = 0.8;      // penalty per unspent mana crystal at the end of the turn
        int width = 5;
        int mulliganMode = 1;        // 1: curve aware; 0: replace what costs 4 or more
        int replyK = 5;              // leaves examined with the opponent's reply (0: none)
        int replyBudget = 700;       // simulations reserved for them
        int lethalWidth = 6;
        int classBurst = 1;          // the opponent's burst from hand depends on its class (0: one guess for all)
        int resample = 1;            // correct the first action of a plan for its random outcomes (0: one sample)
        int resampleN = 3;           // extra samples per first action
        // Normal: the same search, kept shallow and noisy (it plans a few actions ahead, never looks at the
        // opponent's reply, and chooses among its best few plans with some noise).
        int normalWidth = 3;
        int normalDepth = 5;
        int normalBudget = 180;      // simulations per decision
        int normalKeep = 4;          // plans it chooses between
        double normalNoise = 1.0;    // noise added to each plan's score before choosing
    };

    // Penalty per unspent mana crystal in Normal's evaluation.
    constexpr double kSimpleManaWaste = 0.35;
#ifdef HS_AI_TUNING
    extern thread_local Weights* g_tuneWeights; // development only: overrides the defaults
#endif

    Action EndTurnAction();

    // Applied when a profile would end its turn: with a Taunt in the way to the hero, an attack on the weakest Taunt
    // (false: no Taunt blocks, or nothing can attack it).
    bool BreakTaunt(MatchView const& v, Action& out);

    // The simple mulligan (replace every card costing 4 or more).
    Action SimpleMulligan(MatchView const& v, CardDb const& db);

    // Applies action on a simulation copy and answers any pick it leaves pending for side (the picked card is the
    // best by a simple card value), then fills view with side's view of the copy. Counts the Apply calls in sims.
    ActionError ApplyAndView(Match& sim, int side, Action const& a, CardDb const& db, int& sims, MatchView& view);

    // Does a minion of the side destroy every minion at the start of its owner's turn (Doomsayer), not silenced?
    // (HsMatchAiSearch.cpp)
    bool WipesBoard(SideView const& sd);
    // Does the side have a working (not silenced) Kel'Thuzad, who brings back this turn's dead minions at its end?
    // (HsMatchAiSearch.cpp)
    bool HasKelThuzad(SideView const& sd);
    // How likely the AI's own Doomsayer survives the opponent's turn and goes off (the opponent's goes off for sure:
    // its turn starts right after the AI's).
    constexpr double kOwnWipeOdds = 0.75;

    // Normal's plain stat-counting evaluation of a position from the viewer's side (HsMatchAi.cpp).
    double SimpleEvalPosition(MatchView const& v, CardDb const& db);

    // Normal: a shallow, noisy search over its own turn (HsMatchAiSearch.cpp).
    Action ChooseNormal(Match const& match, MatchView const& view, int side, CardDb const& db, int actionsThisTurn,
        uint32_t aiSeed, AiDecisionInfo* info);
    // Normal: answers a pending pick.
    Action ChooseNormalPick(Match const& match, MatchView const& view, int side, CardDb const& db, uint32_t aiSeed);

    // Hard.
    Action ChooseHardMulligan(MatchView const& v, CardDb const& db);
    Action ChooseHardPick(Match const& match, MatchView const& view, int side, CardDb const& db, uint32_t aiSeed);
    Action ChooseHard(Match const& match, MatchView const& view, int side, CardDb const& db, int actionsThisTurn,
        uint32_t aiSeed, AiDecisionInfo* info);

    // Value of a card id for pick decisions (higher is better), 0 for unknown cards.
    double PickValue(CardDb const& db, std::string const& cardId, int mana);
}
}
}

#endif
