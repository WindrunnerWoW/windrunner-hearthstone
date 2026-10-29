/*
 * World NPC Ladder: the row encodings of the ladder tables. See
 * data/sql/character/0015_hearthstone_ladder.sql.
 *
 * PURE C++ (standard library and the ladder rules, no core headers, no database). The store (src/HsLadderStore.cpp)
 * only runs the statements built here and feeds the cells it reads into the parsers here, so the one place that
 * knows the column order is this file, and tests/ladder_tests.cpp can round-trip a whole LadderState through it
 * (state -> statements -> cells -> state, same StateHash) and check every column against the migration.
 *
 * The offers of the ladder chair (data/sql/character/0016_hearthstone_ladder_offer.sql) are encoded here too.
 *
 * Reading: a row is a vector of cell texts in the order of the matching column list (a database field's text; NULL
 * is the empty text). Writing: the statement builders return ready-to-run SQL texts (long inserts are split in
 * chunks); the caller passes the escape function of the database.
 */

#ifndef MOD_HEARTHSTONE_HS_LADDER_ROWS_H
#define MOD_HEARTHSTONE_HS_LADDER_ROWS_H

#include "HsLadderOffers.h"
#include "HsLadderRules.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Hs
{
namespace Ladder
{
    using SqlEscape = std::function<std::string(std::string const&)>;

    // ---- tables and columns (the order of the cells) -----------------------------------------------------

    constexpr char const* kTableLadder = "hearthstone_ladder";
    constexpr char const* kTableEntrant = "hearthstone_ladder_entrant";
    constexpr char const* kTableVs = "hearthstone_ladder_vs";
    constexpr char const* kTableGame = "hearthstone_ladder_game";

    constexpr char const* kLadderColumns = "`ladder_id`, `seed`, `rounds_played`, `started`, `rules_version`";
    constexpr std::size_t kLadderColumnCount = 5;
    constexpr char const* kEntrantColumns =
        "`entrant_id`, `name`, `faction`, `hero_class`, `deck_key`, `profile`, `enabled`, `joined_round`, `rank_no`, "
        "`stars`, `rating`, `games`, `wins`, `losses`, `draws`, `win_streak`, `best_rank`, `best_stars`, `byes`, "
        "`last_opponent`";
    constexpr std::size_t kEntrantColumnCount = 20;
    constexpr char const* kVsColumns = "`player_id`, `npc_id`, `wins`, `losses`, `draws`";
    constexpr std::size_t kVsColumnCount = 5;
    constexpr char const* kGameColumns =
        "`round_no`, `slot`, `rating_only`, `npc_a`, `npc_b`, `seed`, `outcome`, `first_side`, `turns`, `error`";
    constexpr std::size_t kGameColumnCount = 10;

    // Character migration 0016: the offers of the ladder chair, per account.
    constexpr char const* kTableOffer = "hearthstone_ladder_offer";
    constexpr char const* kTableOfferState = "hearthstone_ladder_offer_state";
    constexpr char const* kOfferColumns = "`account_id`, `slot`, `npc_id`";
    constexpr std::size_t kOfferColumnCount = 3;
    constexpr char const* kOfferStateColumns = "`account_id`, `draw_counter`, `drawn_round`, `last_opponent`";
    constexpr std::size_t kOfferStateColumnCount = 4;

    // Text limits of the VARCHAR columns (longer text is cut on a character boundary).
    constexpr std::size_t kNameBytes = 100;
    constexpr std::size_t kClassBytes = 12;
    constexpr std::size_t kDeckKeyBytes = 32;
    constexpr std::size_t kErrorBytes = 160;

    // ---- rows -------------------------------------------------------------------------------------------------

    // The single row of hearthstone_ladder (ladder_id is always 1).
    struct LadderRow
    {
        uint32_t seed = 0;
        uint32_t roundsPlayed = 0;
        bool started = false;
        uint32_t rulesVersion = 0; // Match::kRulesVersion of the last save
    };

    // One hearthstone_ladder_entrant row: every field of Entrant, with the enums as their codes.
    struct EntrantRow
    {
        uint64_t id = 0;
        std::string name;      // the NPC's name at the last save / the character name of a player (runtime only)
        std::string faction;   // alliance, horde, any
        std::string heroClass;
        std::string deckKey;
        std::string profile;   // normal, hard
        bool enabled = true;
        uint32_t joinedRound = 0;
        uint32_t rank = 25; // kBottomRank
        uint32_t stars = 0;
        int32_t rating = kStartRating;
        uint32_t games = 0, wins = 0, losses = 0, draws = 0;
        uint32_t winStreak = 0;
        uint32_t bestRank = 25, bestStars = 0;
        uint32_t byes = 0;
        uint64_t lastOpponent = 0;
    };

    struct VsRow
    {
        uint64_t player = 0;
        uint64_t npc = 0;
        uint32_t wins = 0, losses = 0, draws = 0;
    };

    // One hearthstone_ladder_game row.
    struct GameRow
    {
        uint32_t round = 0;
        uint32_t slot = 0;
        bool ratingOnly = false; // a game of the ladder start
        uint64_t a = 0, b = 0;   // a is the engine's side 0
        uint32_t seed = 0;
        std::string outcome;     // win0, win1, draw, failed
        int firstSide = -1;
        uint32_t turns = 0;
        std::string error;
    };

    // One hearthstone_ladder_offer row: the NPC (entrant id) in a slot of an account's offers.
    struct OfferRow
    {
        uint32_t account = 0;
        uint32_t slot = 0; // 0 .. kOfferCount - 1
        uint64_t npc = 0;
    };

    // The single hearthstone_ladder_offer_state row of an account (exists once the account was offered anything).
    struct OfferStateRow
    {
        uint32_t account = 0;
        uint32_t drawCounter = 0;
        uint32_t drawnRound = 0;
        uint64_t lastOpponent = 0;
    };

    // ---- state <-> rows ----------------------------------------------------------------------------------

    LadderRow ToLadderRow(LadderState const& state, uint32_t rulesVersion);
    EntrantRow ToRow(Entrant const& entrant);
    VsRow ToRow(VsRecord const& vs);
    // The log rows of a played round (one per planned game, in slot order).
    std::vector<GameRow> ToGameRows(RoundPlan const& plan, std::vector<GameResult> const& results);

    // False (error filled) for a row that is not a valid entrant (unknown faction or profile, a rank or star count
    // outside the ladder).
    bool FromRow(EntrantRow const& row, Entrant& out, std::string& error);
    VsRecord FromRow(VsRow const& row);

    // Rebuilds the state a save wrote. False (error filled) for a bad entrant row or a repeated entrant id.
    bool BuildState(LadderRow const& ladder, std::vector<EntrantRow> const& entrants, std::vector<VsRow> const& vs,
                    LadderState& out, std::string& error);

    // The rows of an account's offers: one state row, one row per slot.
    OfferStateRow ToStateRow(OfferSet const& set);
    std::vector<OfferRow> ToOfferRows(OfferSet const& set);
    // Rebuilds the set a save wrote. False (error filled) for rows of another account, a slot number outside
    // 0 .. kOfferCount - 1, a slot stored twice or a gap in the slots (0, 2).
    bool BuildOfferSet(OfferStateRow const& state, std::vector<OfferRow> const& slots, OfferSet& out, std::string& error);

    // ---- cells -> rows (false: a wrong cell count or a cell that is not a number) --------------------------

    bool LadderRowFromCells(std::vector<std::string> const& cells, LadderRow& out);
    bool EntrantRowFromCells(std::vector<std::string> const& cells, EntrantRow& out);
    bool VsRowFromCells(std::vector<std::string> const& cells, VsRow& out);
    bool GameRowFromCells(std::vector<std::string> const& cells, GameRow& out);
    bool OfferRowFromCells(std::vector<std::string> const& cells, OfferRow& out);
    bool OfferStateRowFromCells(std::vector<std::string> const& cells, OfferStateRow& out);

    // ---- statements ---------------------------------------------------------------------------------------

    // Each returns the SQL texts to run (empty for nothing to write). Entrants, vs and games use REPLACE INTO (the
    // key is the row), the ladder row an upsert that keeps created_at.
    std::string LadderUpsertSql(LadderRow const& row);
    std::vector<std::string> EntrantReplaceSql(std::vector<EntrantRow> const& rows, SqlEscape const& escape);
    std::vector<std::string> VsReplaceSql(std::vector<VsRow> const& rows);
    std::vector<std::string> GameReplaceSql(std::vector<GameRow> const& rows, SqlEscape const& escape);
    // DELETE FROM for every ladder table.
    std::vector<std::string> ResetSql();

    // One account's offers in one transaction: its slots are deleted and written again, its state row is upserted.
    std::vector<std::string> OfferSaveSql(OfferSet const& set);
    // DELETE FROM for both offer tables (every account).
    std::vector<std::string> OfferResetSql();

    // ---- what each save writes (one transaction each; the store runs the statements in this order) --------
    //
    // Every save starts with the ladder row. The entrants written are exactly the ones the operation changed, so a
    // database that received all saves in order holds the state that was saved (tests/ladder_tests.cpp checks this
    // against StateHash after every kind of save).

    // A roster change, or the first save of a new ladder: the ladder row and the named entrants.
    std::vector<std::string> RosterSaveSql(LadderState const& state, std::vector<EntrantId> const& changed,
                                           uint32_t rulesVersion, SqlEscape const& escape);
    // One applied round: the ladder row, the entrants in report.touched and one game row per planned game.
    std::vector<std::string> RoundSaveSql(LadderState const& state, RoundPlan const& plan,
                                          std::vector<GameResult> const& results, RoundReport const& report,
                                          uint32_t rulesVersion, SqlEscape const& escape);
    // After PlaceNpcs: the ladder row (started) and every entrant.
    std::vector<std::string> PlacementSaveSql(LadderState const& state, uint32_t rulesVersion, SqlEscape const& escape);
    // One player result: the ladder row, the player, the NPC, the entrants pushed out of Legend and the player's
    // record against the NPC.
    std::vector<std::string> PlayerResultSaveSql(LadderState const& state, EntrantId player, EntrantId npc,
                                                 std::vector<EntrantId> const& pushedOut, uint32_t rulesVersion,
                                                 SqlEscape const& escape);

    // ---- text for the GM commands -----------------------------------------------------------------------

    std::string RatingText(int32_t centiPoints);            // 100000 -> "1000.00", -50 -> "-0.50"
    std::string RankText(int rank, int stars);               // "Legend" or "Rank 7, 3 stars"
    std::string ClassText(std::string const& heroClass);     // "MAGE" -> "Mage"
}
}

#endif
