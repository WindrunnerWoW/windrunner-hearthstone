/*
 * mod-hearthstone: database glue of the Draft Arena. The rules, the run state machine and the
 * row encodings live in src/arena (pure C++); this file only reads and writes rows.
 *
 * Tables (CHARACTER database, keyed by account id, data/sql/character/0010_hearthstone_arena.sql):
 *   hearthstone_arena_run, _entry (the daily slot), _pick, _match (the AI opponent of a duel), _action (its replay
 *   log), _milestone (special-card claims), _op (client op keys); _quest (the owned quest cards a
 *   run snapshotted at entry, 0012_hearthstone_arena_quest.sql, written with the run row).
 *
 * HsArenaDbStorage is the ArenaStorage of the run service AND the HsArenaDuelSink of the match glue: the actions of
 * an arena duel are written (synchronously, one statement per action) before the player sees them, and a suspended
 * duel stores its timer and pauses the run.
 *
 * Entry: CreateRun is one transaction (run row, daily slot, class offer). SaveRun normally opens its own
 * transaction; inside an HsArenaStoreJoin scope it only QUEUES its statements into the transaction the caller has
 * open, so that the entry confirmation commits together with the character's debit (HsArenaGlue.cpp).
 */

#ifndef MOD_HEARTHSTONE_HS_ARENA_STORE_H
#define MOD_HEARTHSTONE_HS_ARENA_STORE_H

#include "HsMatchGlue.h"
#include "arena/HsArenaRows.h"
#include "arena/HsArenaService.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// While one is alive on a thread, HsArenaDbStorage::SaveRun on that thread joins the character database transaction
// that is open (CharacterDatabase.InTransaction()) instead of beginning and committing its own: it queues the
// statements and returns whether they were queued; the caller commits or rolls back. payingGuid (when not 0) is
// written as the run's paying_guid, so that a retry by another character records who really paid.
class HsArenaStoreJoin
{
public:
    explicit HsArenaStoreJoin(uint32_t payingGuid);
    ~HsArenaStoreJoin();
    HsArenaStoreJoin(HsArenaStoreJoin const&) = delete;
    HsArenaStoreJoin& operator=(HsArenaStoreJoin const&) = delete;
};

class HsArenaDbStorage : public Hs::ArenaStorage, public HsArenaDuelSink
{
public:
    // ---- Hs::ArenaStorage ------------------------------------------------------------------------------------
    bool LoadLatestRun(uint32_t accountId, bool& found, Hs::ArenaRun& out) override;
    bool IsSlotUsed(uint32_t accountId, uint64_t period, bool& used) override;
    Hs::ArenaStoreResult CreateRun(Hs::ArenaRun& run) override;
    bool SaveRun(Hs::ArenaRun const& run) override;
    bool DeleteUnconfirmedRun(uint32_t accountId, uint32_t runId) override;

    // ---- the duels of a run ------------------------------------------------------------------------------------
    // The stored opponent of a duel. found = false for a duel that has no row yet.
    bool LoadOpponent(uint32_t runId, uint32_t matchNo, bool& found, Hs::ArenaOpponent& out);
    // Inserts the opponent row. False if the row exists already or the write failed.
    bool SaveOpponent(Hs::ArenaOpponent const& opponent);
    // The decided result of a duel ("win", "draw" or "loss").
    bool SetResult(uint32_t runId, uint32_t matchNo, char const* result);
    // The remaining turn timer of a duel (0 once a restored duel runs again).
    bool SetTimer(uint32_t runId, uint32_t matchNo, uint32_t timerMs);
    // The stored replay log in idx order, as text. False on a database failure or a gap in idx.
    bool LoadActions(uint32_t runId, uint32_t matchNo, std::vector<std::string>& out);
    // Empties the log of a duel that is about to be (re)started from its beginning.
    bool ResetActions(uint32_t runId, uint32_t matchNo);
    bool CountActions(uint32_t runId, uint32_t matchNo, uint32_t& count);
    // The duel of runId that the match glue plays now: StoreActions needs the match number (a log entry carries
    // none). Set before the match starts or is restored, cleared when it is suspended, ended or dropped.
    void SetActiveMatch(uint32_t runId, uint32_t matchNo);
    void ClearActiveMatch(uint32_t runId);
    bool ActiveMatch(uint32_t runId, uint32_t& matchNo);
    std::size_t ActiveCount();

    // Startup: no duel is live in a fresh process, so every run still in state duel (a crash, a kill) becomes
    // paused. count = the runs changed. False on a database failure.
    bool RecoverDuels(uint32_t& count);

    // ---- HsArenaDuelSink ---------------------------------------------------------------------------------------
    bool StoreActions(uint32_t guid, uint64_t runId, uint32_t first, std::vector<Hs::LoggedAction> const& actions) override;
    void StoreSuspended(Hs::SuspendedDuel const& duel, char const* why) override;

private:
    std::mutex m_mutex; // the active matches; a leaf lock
    std::map<uint32_t, uint32_t> m_active;
};

// The process-wide arena storage and run service (backed by HsArenaDbStorage).
HsArenaDbStorage& HsArenaStore();
Hs::ArenaService& HsArena();

// The number of confirmed runs of the account that ended (finished, retired or settled) with at least minWins wins.
// False (logged) on a database failure.
bool HsArenaCountGoodRuns(uint32_t accountId, uint32_t minWins, uint32_t& count);

// The number of paid entries (gold or coins, confirmed) of the account, whatever state the run is in. Free and practice
// runs are not counted. False (logged) on a database failure.
bool HsArenaCountPaidEntries(uint32_t accountId, uint32_t& count);

// Logs an error for every character table of the arena that is missing; false if any is.
bool CheckHsArenaTables();

#endif
