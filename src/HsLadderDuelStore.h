/*
 * mod-hearthstone: database glue of the ranked ladder duels that pause and resume. The row encodings
 * and the statements are src/ladder/HsLadderDuel.* (pure C++); this file runs them. See
 * data/sql/character/0017_hearthstone_ladder_duel.sql.
 *
 * It is the HsArenaDuelSink of the ladder duels: the match glue hands it every action of a ladder duel (run id with
 * Hs::Ladder::kLadderRunBit set) BEFORE the payload goes out, and every suspension. The draft arena has its own sink; the
 * match glue routes by the run id.
 *
 * One ladder duel per account (a unique key), kept in memory as account -> duel id so the match start checks (every
 * ordinary, arena and ladder start) cost nothing. The duel row and its log are deleted in the transaction that applies the
 * result to the ladder (HsLadderGlue.cpp), never separately, so a result is applied exactly once.
 *
 * Threads: StoreActions of a GAME_ACT runs under the session registry mutex, StoreSuspended and StoreActions of an AI action
 * on the world thread or the thread of the player's hook (see HsArenaDuelSink in HsMatchGlue.h). Every call is synchronous
 * and takes only the leaf mutex of this class.
 */

#ifndef MOD_HEARTHSTONE_HS_LADDER_DUEL_STORE_H
#define MOD_HEARTHSTONE_HS_LADDER_DUEL_STORE_H

#include "HsMatchGlue.h"
#include "ladder/HsLadderDuel.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

class HsLadderDuelStorage final : public HsArenaDuelSink
{
public:
    // OnStartup (the ladder must be enabled): checks the tables of migration 0017, reads the duels, pauses the ones a stop or
    // a crash left active and sets the id counter. False (logged): the ladder duels stay off (the chairs refuse to start one).
    bool Startup();
    // The tables are there and the duels were read.
    bool Ready();

    // One-duel-per-account: does the account have a ladder duel (any state).
    bool Has(uint32_t accountId);
    // The duel of the account (read from the database). found = false: none. False (logged): a database failure or a row
    // that is not valid.
    bool Find(uint32_t accountId, bool& found, Hs::Ladder::DuelRow& out);
    // The duel with that id (read from the database).
    bool FindById(uint64_t duelId, bool& found, Hs::Ladder::DuelRow& out);
    // Every stored duel (the GM list and the startup check). False (logged) on a failure.
    bool LoadAll(std::vector<Hs::Ladder::DuelRow>& out);
    // The stored log rows of a duel, in order.
    bool LoadLog(uint64_t duelId, std::vector<std::string>& rows);
    // The stored number of log rows.
    bool CountLog(uint64_t duelId, uint32_t& count);

    // Takes the next duel id and writes the row (row.id is set, state active). False: the account has a duel already, or
    // the write failed (logged).
    bool Begin(Hs::Ladder::DuelRow& row);
    // state = active, the character that plays it, the timer cleared (a resume).
    bool MarkActive(uint64_t duelId, uint32_t guid);
    // Removes a duel without a result (a start that failed, a GM abandon): its row and its log.
    bool Abandon(uint64_t duelId);
    // The duel was deleted by the transaction of its result: forget it in memory (the account may start a new one).
    void Settled(uint32_t accountId);
    // DELETE FROM for the duel tables and the pending rounds (a ladder reset).
    bool Reset();

    // The queued ladder work that was not applied or played before the server stopped, oldest first.
    bool LoadQueue(std::vector<Hs::Ladder::QueueRow>& out);

    // HsArenaDuelSink.
    bool StoreActions(uint32_t guid, uint64_t runId, uint32_t first, std::vector<Hs::LoggedAction> const& actions) override;
    void StoreSuspended(Hs::SuspendedDuel const& duel, char const* why) override;

private:
    std::mutex m_mutex;
    bool m_ready = false;
    uint64_t m_nextId = 1;
    std::map<uint32_t, uint64_t> m_byAccount; // account -> duel id
};

// The process-wide store.
HsLadderDuelStorage& HsLadderDuels();

// Logs an error for every table of migration 0017 that is missing; false if any is.
bool CheckHsLadderDuelTables();

// The statements that store the queue (every row of hearthstone_ladder_queue replaced by rows, in order). They go into the
// transaction of the change they describe (HsLadderStore().Run, SavePlayerResult, SaveRound).
std::vector<std::string> LadderQueueStatements(std::vector<Hs::Ladder::QueueRow> const& rows);

#endif
