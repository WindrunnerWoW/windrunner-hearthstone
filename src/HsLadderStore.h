/*
 * mod-hearthstone: database glue of the World NPC Ladder. The rules and the row encodings
 * (the column order, the statements, the checks of a loaded row) live in src/ladder (pure C++); this file only
 * reads and writes rows.
 *
 * Tables (CHARACTER database, data/sql/character/0015_hearthstone_ladder.sql): hearthstone_ladder (one row),
 * hearthstone_ladder_entrant (an NPC or a player each), hearthstone_ladder_vs (a player's record against an NPC) and
 * hearthstone_ladder_game (the log of the NPC games).
 *
 * Every save is ONE synchronous transaction, as in HsArenaStore.cpp (the statements are queued, then
 * CommitTransactionDirect runs them inside BEGIN / COMMIT and rolls back on the first failure): a round (ladder row,
 * the entrants it touched, its game rows), a player result (ladder row, both entrants, pushed-out entrants, the
 * player's record against the NPC), a roster change and the placement after the ladder start. A crash loses at most
 * the save that was running. All calls come from one thread at a time (the glue serializes them) and use that
 * thread's character database transaction.
 *
 * Adds the offers of the ladder chair (data/sql/character/0016_hearthstone_ladder_offer.sql): per account the
 * three offered NPCs (hearthstone_ladder_offer) and the draw counter, the round drawn and the last opponent
 * (hearthstone_ladder_offer_state). An account's offers are saved in one transaction.
 */

#ifndef MOD_HEARTHSTONE_HS_LADDER_STORE_H
#define MOD_HEARTHSTONE_HS_LADDER_STORE_H

#include "ladder/HsLadderRows.h"

#include <cstdint>
#include <string>
#include <vector>

class HsLadderDbStore
{
public:
    // Reads the whole ladder. found = false: there is no ladder row (a new server or after a reset). false (logged):
    // a database failure or a row that is not valid; the glue then leaves the ladder off. rulesVersion is the engine
    // rules version the ladder was last saved with.
    bool Load(bool& found, Hs::Ladder::LadderState& out, uint32_t& rulesVersion);

    // The ladder row and the named entrants (a roster change; the first save of a new ladder).
    bool SaveRoster(Hs::Ladder::LadderState const& state, std::vector<Hs::Ladder::EntrantId> const& changed);
    // A round: the ladder row, the entrants in report.touched and one game row per planned game.
    // extra: statements written in the same transaction (the pending round row of a round that was waiting).
    bool SaveRound(Hs::Ladder::LadderState const& state, Hs::Ladder::RoundPlan const& plan,
                   std::vector<Hs::Ladder::GameResult> const& results, Hs::Ladder::RoundReport const& report,
                   std::vector<std::string> const& extra = {});
    // After PlaceNpcs: the ladder row (started) and every entrant.
    bool SavePlacement(Hs::Ladder::LadderState const& state);
    // A player result: the ladder row, the player, the NPC, the entrants pushed out of Legend and the vs row.
    // extra: statements written in the same transaction (the duel of the result is deleted, the NPC round that
    // follows is written as pending), so the result is applied exactly once.
    bool SavePlayerResult(Hs::Ladder::LadderState const& state, Hs::Ladder::EntrantId player, Hs::Ladder::EntrantId npc,
                          std::vector<Hs::Ladder::EntrantId> const& pushedOut, std::vector<std::string> const& extra = {});
    // Deletes every ladder row (all four tables) in one transaction.
    bool Reset();
    // Runs statements in one synchronous transaction (the ladder duel store and the settlement use it).
    bool Run(std::vector<std::string> const& statements, char const* what) { return Commit(statements, what); }

    // The newest games (highest round first) of an NPC, at most limit. false (logged) on a database failure.
    bool LoadGames(Hs::Ladder::EntrantId npc, uint32_t limit, std::vector<Hs::Ladder::GameRow>& out);

    // The offers of an account. found = false: nothing stored for the account (never offered anything). false
    // (logged): a database failure or rows that are not valid.
    bool LoadOffers(uint32_t accountId, bool& found, Hs::Ladder::OfferSet& out);
    // Replaces the slots and the state row of set.account in one transaction.
    bool SaveOffers(Hs::Ladder::OfferSet const& set);
    // Deletes the offers of every account (both offer tables) in one transaction, for a ladder reset.
    bool ResetOffers();

    // Character migration 0018: the milestones an account reached. False (logged) on a failure or a missing table.
    bool LoadMilestones(uint32_t accountId, std::vector<std::string>& out);
    // INSERT IGNORE of one milestone. False (logged) on a failure.
    bool SaveMilestone(uint32_t accountId, std::string const& milestone);

private:
    bool Commit(std::vector<std::string> const& statements, char const* what);
};

// The process-wide ladder store.
HsLadderDbStore& HsLadderStore();

// Logs an error for every character table of the ladder that is missing; false if any is.
bool CheckHsLadderTables();
// The same for the two offer tables (migration 0016): the ladder itself works without them, the chairs do not.
bool CheckHsLadderOfferTables();

#endif
