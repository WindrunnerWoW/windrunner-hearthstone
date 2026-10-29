/*
 * World NPC Ladder: the row encodings of the ranked ladder duels that pause and resume. See
 * data/sql/character/0017_hearthstone_ladder_duel.sql.
 *
 * PURE C++ (standard library, the ladder rows and the match service header for MatchStartParams / SuspendedDuel; nothing of
 * the core, no database). The store (src/HsLadderDuelStore.cpp) runs the statements built here and feeds the cells it
 * reads into the parsers here.
 *
 * A ladder duel is a draft-arena-style durable match (MatchStartParams::arena = true) whose run id is the duel id with
 * kLadderRunBit set, so the match glue can tell the two kinds of duel apart while the match service stays unchanged.
 */

#ifndef MOD_HEARTHSTONE_HS_LADDER_DUEL_H
#define MOD_HEARTHSTONE_HS_LADDER_DUEL_H

#include "HsLadderRows.h"
#include "../match/HsMatchService.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Hs
{
namespace Ladder
{
    // The run id of a ladder duel is kLadderRunBit | duel id (an arena run id is a 32-bit number).
    constexpr uint64_t kLadderRunBit = 1ull << 40;
    inline bool IsLadderRun(uint64_t runId) { return (runId & kLadderRunBit) != 0; }
    inline uint64_t DuelIdOfRun(uint64_t runId) { return runId & ~kLadderRunBit; }
    inline uint64_t RunOfDuel(uint64_t duelId) { return duelId | kLadderRunBit; }

    constexpr char const* kTableDuel = "hearthstone_ladder_duel";
    constexpr char const* kTableDuelAction = "hearthstone_ladder_duel_action";
    constexpr char const* kTableQueue = "hearthstone_ladder_queue";

    constexpr char const* kDuelColumns =
        "`duel_id`, `account_id`, `guid`, `state`, `npc_entry`, `opponent_name`, `opponent_class`, `opponent_deck_key`, "
        "`opponent_deck`, `profile`, `deck_id`, `deck_name`, `human_class`, `human_deck`, `seed`, `started_at`, "
        "`rules_version`, `dataset_hash`, `timer_ms`";
    constexpr std::size_t kDuelColumnCount = 19;
    constexpr std::size_t kDuelActionBytes = 160;
    constexpr char const* kQueueColumns =
        "`seq`, `kind`, `excluded_npc`, `account_id`, `player_name`, `npc_id`, `result`, `duel_id`";
    constexpr std::size_t kQueueColumnCount = 8;
    constexpr uint32_t kQueueRound = 0;   // kind of an NPC round
    constexpr uint32_t kQueueResult = 1;  // kind of a player result

    constexpr char const* kDuelActive = "active";
    constexpr char const* kDuelPaused = "paused";

    // One hearthstone_ladder_duel row: everything a duel needs to be rebuilt by replaying its log.
    struct DuelRow
    {
        uint64_t id = 0;
        uint32_t account = 0;
        uint32_t guid = 0;          // the character that plays it
        std::string state = kDuelActive;
        uint32_t npcEntry = 0;
        std::string opponentName;
        std::string opponentClass;
        std::string opponentDeckKey;
        std::vector<std::string> opponentDeck; // one entry per copy
        std::string profile;        // normal, hard
        uint32_t deckId = 0;
        std::string deckName;
        std::string humanClass;
        std::vector<std::string> humanDeck;
        uint32_t seed = 0;
        int64_t startedAt = 0;
        uint32_t rulesVersion = 0;
        std::string datasetHash;
        uint32_t timerMs = 0;
    };

    // One item of the ladder queue (hearthstone_ladder_queue, in seq order): an NPC round that waits, or a player result
    // that waits for the running round. Which fields are used follows kind (see migration 0019).
    struct QueueRow
    {
        uint64_t seq = 0;
        uint32_t kind = kQueueRound;
        uint64_t excludedNpc = 0;   // round: the NPC that sits out (0 for none)
        uint32_t account = 0;       // result
        std::string playerName;     // result
        uint64_t npcId = 0;         // result: the NPC entrant id
        uint8_t result = 0;         // result: 0 win, 1 draw, 2 loss (Hs::Ladder::Result)
        uint64_t duelId = 0;        // result: the duel it settles, 0 for none
    };

    // The rules version and dataset hash a duel is recorded with are the current ones (the engine's).
    // True: the log of the duel can be replayed.
    bool DuelReplayable(DuelRow const& row, uint32_t rulesVersion, std::string const& datasetHash);

    // The row of a duel about to start: the start parameters (the human's and the opponent's deck, the seed, the meta).
    // id and the account come from the parameters' meta / the caller.
    DuelRow DuelFromParams(MatchStartParams const& params, uint64_t id, uint32_t npcEntry, uint32_t rulesVersion,
                           std::string const& datasetHash);
    // The start parameters of a stored duel for the character (name: the character's name now). The match is a durable
    // match of run RunOfDuel(row.id); difficulty and the AI profile follow the stored profile.
    MatchStartParams ParamsFromDuel(DuelRow const& row, uint32_t guid, std::string const& humanName);
    // The SuspendedDuel to restore: those parameters, the decoded log and the stored remaining timer.
    SuspendedDuel SuspendedFromDuel(DuelRow const& row, uint32_t guid, std::string const& humanName,
                                    std::vector<LoggedAction> const& log);

    // "A,B,C" <-> {A,B,C}. SplitCards gives false for an empty entry or a card id with a character that is not a
    // letter, digit, underscore or hyphen.
    std::string JoinCards(std::vector<std::string> const& cards);
    bool SplitCards(std::string const& text, std::vector<std::string>& out);

    // The decoded log of a duel (Hs::DecodeAction per row). False with the index of the row that does not decode.
    bool DecodeDuelLog(std::vector<std::string> const& rows, std::vector<LoggedAction>& out, std::size_t& failedAt,
                       std::string& error);

    bool DuelRowFromCells(std::vector<std::string> const& cells, DuelRow& out);
    bool QueueRowFromCells(std::vector<std::string> const& cells, QueueRow& out);

    // ---- statements -------------------------------------------------------------------------------------

    // INSERT of a new duel row (state as in the row).
    std::string DuelInsertSql(DuelRow const& row, SqlEscape const& escape);
    // INSERT of the log entries first.. (one statement; empty for none). An entry that does not encode, or is longer than
    // kDuelActionBytes, gives an empty text and false in ok.
    std::string DuelActionsInsertSql(uint64_t duelId, uint32_t first, std::vector<LoggedAction> const& actions, bool& ok);
    // The statements of a duel that is paused: the state, the remaining timer and (a failed store: keptActions) the
    // log trimmed to what the player was shown.
    std::vector<std::string> DuelPauseSql(uint64_t duelId, uint32_t timerMs, bool trim, uint32_t keptActions);
    // state = active (a resume), timer cleared.
    std::string DuelActiveSql(uint64_t duelId, uint32_t guid);
    // DELETE of the duel row and its log.
    std::vector<std::string> DuelDeleteSql(uint64_t duelId);
    // What the transaction of a counted result writes besides the ladder rows: the duel and its log are deleted (duelId 0,
    // a GM result: nothing). The queue is written separately (QueueSnapshotSql).
    std::vector<std::string> SettleSql(uint64_t duelId);
    // The queue as it must be stored: DELETE of every row, then one INSERT of rows in order (none: only the DELETE). The
    // rows are written in the transaction of the change they describe, so a stop never leaves half of it.
    std::vector<std::string> QueueSnapshotSql(std::vector<QueueRow> const& rows, SqlEscape const& escape);
    // DELETE FROM for the duel tables and the queue (a ladder reset).
    std::vector<std::string> DuelResetSql();

    // ---- milestones (character migration 0018) ------------------------------------------------------------------------
    // One-time records of an account: reaching Legend, and being the number one of Legend. No reward yet (an owner decision):
    // the record is stored, shown to the player when it happens and by `.hs ladder player`.

    constexpr char const* kTableMilestone = "hearthstone_ladder_milestone";
    constexpr char const* kMilestoneLegend = "legend";
    constexpr char const* kMilestoneLegendFirst = "legend1";
    // Which milestones a player's position earns: Legend for rank Legend, Legend #1 when also the first of the ladder.
    std::vector<std::string> MilestonesFor(int rank, bool first);
    // INSERT IGNORE: a milestone is written once, whatever happens again.
    std::string MilestoneInsertSql(uint32_t account, std::string const& milestone);
    // "You reached Legend." / "You are the number one of Legend." ("" for an unknown milestone).
    std::string MilestoneText(std::string const& milestone);

    // ---- text -----------------------------------------------------------------------------------------------

    // "paused duel against Name (class, 12 actions)" for the window and the GM command.
    std::string DuelText(DuelRow const& row, uint32_t actions);
}
}

#endif
