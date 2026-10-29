/*
 * World NPC Ladder: the headless game runner. Plays one complete match between two NPC
 * entrants, both driven by the opponent AI, without a player session, a board, addon pushes or the live
 * service's pacing.
 *
 * PURE C++ (standard library, the match engine and the AI, no core headers), so it runs in tests and on worker
 * threads: a game owns its Match, the CardDb is only read, and neither the engine nor the AI keeps mutable global
 * state. Callers may run any number of games at once on separate threads with the same CardDb.
 *
 * Determinism: the result depends only on the CardDb, both entrants, the seed and the first side, plus the
 * engine rules version and the AI code (Match::kRulesVersion). Every AI decision is seeded from the game seed,
 * the side and the match sequence number; nothing reads a clock or a global RNG. The same inputs give the same
 * SimResult, including stateHash.
 *
 * Outcomes: Win0 / Win1 / Draw are real game results (a draw is both heroes dying together or the engine's turn
 * limit, Match::kMaxTurns). Failed means the game could not be played to a real end: a setup the engine refuses,
 * an action the engine refuses, or the runner's action bound. A failure is never turned into a loss, a concession
 * or a draw; the ladder must treat it as a failed job ("How the NPC league runs").
 *
 * Not simulated: the Nozdormu turn timer (Match::TurnTimeLimitSec). The live service times only the human's
 * turns, never the AI's, so an AI-versus-AI game has no timed turns either.
 */

#ifndef MOD_HEARTHSTONE_HS_LADDER_SIM_H
#define MOD_HEARTHSTONE_HS_LADDER_SIM_H

#include "../match/HsMatch.h"
#include "../match/HsMatchAi.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Hs
{
namespace Ladder
{
    // One side of a simulated game: an NPC's name, hero class, pinned deck and fixed AI profile.
    struct SimEntrant
    {
        std::string name;
        std::string heroClass;         // MAGE ... WARRIOR
        std::vector<std::string> deck; // card ids, one entry per copy (a checked library deck)
        Match::AiProfile profile = Match::AiProfile::Normal;
    };

    enum class SimOutcome : uint8_t
    {
        Win0,  // side 0 (the first entrant) won
        Win1,  // side 1 won
        Draw,  // a real draw (both heroes died, or the turn limit)
        Failed // no real result; see SimResult::error
    };
    char const* SimOutcomeCode(SimOutcome outcome); // "win0", "win1", "draw", "failed"

    // Upper bound on the actions of one game (both sides, mulligans and EndTurn included). A real game stays far
    // below it: at most kMaxTurns turns of at most kAiMaxActionsPerTurn + 1 actions each.
    constexpr int kSimMaxActions = (Match::kMaxTurns + 2) * (Match::kAiMaxActionsPerTurn + 1);

    struct SimResult
    {
        SimOutcome outcome = SimOutcome::Failed;
        Match::EndReason reason = Match::EndReason::None;
        int firstSide = -1;      // the side that took the first turn (-1 when the setup failed)
        int turns = 0;           // Match::Turn() at the end
        int actions = 0;         // actions applied, both sides
        int decisions[2] = {0, 0};      // AI decisions per side
        long simulations[2] = {0, 0};   // Apply calls the AI made on copies, per side
        int heroHealth[2] = {0, 0};     // at the end
        uint64_t stateHash = 0;  // FNV-1a 64 of the final Match::DebugState (0 when the setup failed)
        std::string error;       // Failed: what went wrong
    };

    // The AI seed of one decision of side: a mix of the game seed, the side and the match sequence number.
    uint32_t SimAiSeed(uint32_t gameSeed, int side, uint32_t seq);

    // Test hook: replaces the AI's decision for both sides (nullptr: the AI). Receives the match, the side to act
    // and the actions that side made this turn so far.
    using SimChooser = std::function<Match::Action(Match::Match const& match, int side, int actionsThisTurn)>;

    struct SimOptions
    {
        int maxActions = kSimMaxActions;
        SimChooser chooser;
    };

    // Plays a whole game: entrant a is side 0, b is side 1. firstSide 0 or 1 forces who starts; -1 lets the game
    // seed decide (the coin toss is drawn either way, so the rest of the random sequence is the same).
    SimResult SimulateGame(Match::CardDb const& db, SimEntrant const& a, SimEntrant const& b, uint32_t seed,
                           int firstSide = -1, SimOptions const& options = SimOptions());
}
}

#endif
