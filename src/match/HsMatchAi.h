/*
 * Opponent AI (profiles Normal and Hard).
 *
 * PURE C++. The AI decides from what its side may know only: its own
 * view (Match::ViewFor) and a redacted copy (Match::RedactedCopy) to simulate
 * on, so it can never see the human's hand, either deck's order or future
 * random outcomes. It always returns an action the engine accepts, and it is
 * deterministic for the same match state and aiSeed (no clock, no global RNG).
 *
 * Both profiles work for every card the engine supports (they only use the legal
 * action list, never card knowledge beyond the card definition): they apply
 * candidate actions to redacted copies and evaluate the resulting positions.
 *  - Normal: a shallow, noisy search (HsMatchAiSearch.cpp): a beam of width 3 over its own turn
 *    (at most 5 actions deep, kAiNormalMaxSimulations copies) ranked by a plain stat-counting
 *    evaluation with only a basic sense of survival (the enemy board against its health, Taunts
 *    soaking damage). It has no lethal stage, never looks at the opponent's reply and chooses
 *    among its best few plans with noise, so it errs between close plans but cannot be walled
 *    by one Taunt or ignore a lethal threat on the board.
 *  - Hard: searches its own turn (HsMatchAiSearch.cpp): a lethal stage (beam search for
 *    a kill this turn), then a beam search over action sequences ranked by a richer
 *    evaluation (board with keywords, cards, hero health, mana use, overload, secrets,
 *    threat and pressure; the opponent's burst from hand is guessed by class), then the best few end
 *    positions are looked at again after the opponent's greedy reply with its board, and a first
 *    action with a random outcome is valued by its mean over a few samples, not the one the search
 *    saw. At most kAiHardMaxSimulations Apply calls on copies per decision; the plan is made again
 *    after every real action.
 *    Curve-aware mulligan.
 * Copies reshuffle the AI's own deck and reseed the RNG, so random outcomes in a
 * simulation are not the real ones.
 */

#ifndef MOD_HEARTHSTONE_HS_MATCH_AI_H
#define MOD_HEARTHSTONE_HS_MATCH_AI_H

#include "HsMatch.h"

#include <cstdint>

namespace Hs
{
namespace Match
{
    // At kAiMaxActionsPerTurn or more actions in a turn the AI always ends its turn
    // (a hard bound on top of any heuristic or search, so a turn always ends).
    constexpr int kAiMaxActionsPerTurn = 30;

    // Normal: greedy play (simple, forgiving). Hard: smarter decisions
    // (search over its own turn on a redacted copy, better evaluation, better mulligan).
    // Bosses use a fixed profile chosen by their encounter.
    enum class AiProfile : uint8_t { Normal, Hard };
    char const* AiProfileCode(AiProfile p); // "normal", "hard"

    // The simple entry point: Normal profile from a view only.
    //  - view.mulliganPending: a Mulligan action (replace is a subset of view.mulligan);
    //  - view.pickPending: a Choose action (source is one of view.pick);
    //  - not the viewer's turn or the match is over: EndTurn (the caller must not apply it then);
    //  - otherwise one action of view.legal, or EndTurn when nothing is worth doing.
    Action ChooseAiAction(MatchView const& view, CardDb const& db, int actionsThisTurn);

    // The entry point used by the match service: the next action for side of match
    // with the given profile. Builds side's view and, when the profile needs it, redacted
    // copies (seeded from aiSeed) to simulate on; never reads hidden state any other way.
    // The same rules for the returned action as above. Work per call is bounded (Hard: at
    // most kAiHardMaxSimulations Apply calls on copies).
    constexpr int kAiHardMaxSimulations = 1500;
    constexpr int kAiNormalMaxSimulations = 180;
    Action ChooseAiAction(Match const& match, int side, CardDb const& db, int actionsThisTurn, AiProfile profile,
        uint32_t aiSeed);

    // What a decision cost (diagnostics and tests).
    struct AiDecisionInfo
    {
        int simulations = 0;  // Apply calls made on copies
        bool lethal = false;  // Hard: a sequence that wins this turn was found
    };
    // The same, reporting into info (may be null).
    Action ChooseAiAction(Match const& match, int side, CardDb const& db, int actionsThisTurn, AiProfile profile,
        uint32_t aiSeed, AiDecisionInfo* info);
}
}

#endif
