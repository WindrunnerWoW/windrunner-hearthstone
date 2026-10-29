/*
 * World NPC Ladder: the pure helpers of the chair window and the walk-up.
 *
 * PURE C++ (standard library and the ladder rules, no core headers, no database, no clock). The glue
 * (src/HsLadderTable.cpp) shows the texts in the gossip window and walks the NPC; what can be decided without the core
 * is decided here so tests/ladder_tests.cpp can check it:
 *   - which match results count for the ladder (a real end; an interruption does not),
 *   - the texts of the offer window (the standing line and one line per offer),
 *   - the points the opponent walks, in and back.
 */

#ifndef MOD_HEARTHSTONE_HS_LADDER_PLAY_H
#define MOD_HEARTHSTONE_HS_LADDER_PLAY_H

#include "HsLadderWorld.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Hs
{
namespace Ladder
{
    // The npc_text rows of the chair window (world migration 0050_hearthstone_ladder_chair_texts.sql).
    constexpr uint32_t kChairTextOffers = 9195930;
    constexpr uint32_t kChairTextDecks = 9195931;
    constexpr uint32_t kChairTextConfirm = 9195932;
    // World migration 0051_hearthstone_ladder_duel_texts.sql: the window of a paused duel and the surrender question.
    constexpr uint32_t kChairTextPaused = 9195933;
    constexpr uint32_t kChairTextSurrender = 9195934;

    // The player's result of an ended match (MatchEnd::res, from the human's point of view): "win", "loss" and "draw"
    // count; a surrender is a loss (res "loss", why "concede"). "int" (logout, teleport, death, the NPC gone, Stuck,
    // a GM, a config reload) and anything else give false: no ladder result.
    bool PlayerResultOfMatch(std::string const& res, Result& out);

    // "3-1-0" (wins - losses - draws).
    std::string RecordText(uint32_t wins, uint32_t losses, uint32_t draws);
    // "Legend" or "Rank 7 (3 stars)".
    std::string PositionText(int rank, int stars);
    // "Alliance", "Horde" or "Neutral".
    std::string FactionText(Faction faction);

    // The first line of the window: "Your standing: Rank 7 (3 stars), record 4-2-0." or, without a ladder entry,
    // "You are not on the ladder yet (you start at Rank 15 (0 stars))."
    std::string StandingText(bool onLadder, int rank, int stars, uint32_t wins, uint32_t losses, uint32_t draws);

    // One offer: "<name> - <class> - <faction> - <rank> - your record 1-0-0". heroClass is the stored upper case
    // class ("MAGE"). The text is cut to maxBytes (a gossip option holds about 255 bytes; the name is cut first).
    std::string OfferLineText(std::string const& name, std::string const& heroClass, Faction faction, int rank, int stars,
                              uint32_t wins, uint32_t losses, uint32_t draws, std::size_t maxBytes = 120);

    // The points the opponent walks from the spawn point to the seat: the path points, the stop point and the seat
    // (the spawn point itself is where it starts, so it is not a leg). The seat is the last leg unless it is the
    // stop point.
    std::vector<WorldPoint> WalkInPoints(LadderTable const& table, WorldPoint const& seat);

    // The way back after `reached` legs of WalkInPoints were walked (0 = still on the first leg): the points already
    // passed, nearest first, then the spawn point (always last). reached larger than the legs is cut to them.
    std::vector<WorldPoint> WalkBackPoints(LadderTable const& table, std::vector<WorldPoint> const& walkIn,
                                           std::size_t reached);

    // Straight distance in yards.
    float Distance(WorldPoint const& a, WorldPoint const& b);
}
}

#endif
