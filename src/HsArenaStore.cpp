#include "HsArenaStore.h"

#include "HsArenaHost.h"

#include "Database/DatabaseEnv.h"
#include "Log.h"

#include <memory>
#include <string>
#include <utility>

namespace
{
    // The joined-transaction scope of HsArenaStoreJoin (per thread: a character database transaction is per thread).
    thread_local unsigned t_joinDepth = 0;
    thread_local uint32_t t_joinPayer = 0;

    std::string Escaped(std::string s, std::size_t maxLen)
    {
        if (s.size() > maxLen)
            s.resize(maxLen);
        CharacterDatabase.escape_string(s);
        return s;
    }

    std::string TextOf(Field const& f)
    {
        return f.IsNULL() ? std::string() : f.GetCppString();
    }

    unsigned long long Wide(uint64_t value)
    {
        return static_cast<unsigned long long>(value);
    }

    // Runs the queued statements synchronously inside BEGIN/COMMIT (rolls back on the first failing statement), or
    // rolls back if something was not queued. Same shape as HsCollectionStore.cpp.
    bool FinishTransaction(bool queued, char const* what, uint32_t runId)
    {
        if (!queued)
        {
            CharacterDatabase.RollbackTransaction();
            sLog.outError("Hearthstone: could not queue the %s of arena run %u", what, runId);
            return false;
        }
        if (!CharacterDatabase.CommitTransactionDirect())
        {
            sLog.outError("Hearthstone: commit failed for the %s of arena run %u; its outcome is unknown, the run is read "
                "from the database again", what, runId);
            return false;
        }
        return true;
    }

    // Columns of hearthstone_arena_run in the order ReadRun reads them.
    char const* const kRunColumns =
        "`run_id`, `state`, `repair_from`, `deck_class`, `wins`, `draws`, `losses`, `match_no`, `result_no`, "
        "`pay_method`, `pay_amount`, `base_packs`, `bonus_coins`, `rarity_weights`, `rules_version`, `dataset_hash`, "
        "`paying_guid`, `slot_period`, `entry_confirmed`, `controller_guid`, `ended_guid`, `retired`, `prize_packs`, "
        "`prize_coins`, `prize_special`, `special_pending`, `special_card`, `special_copies`, `created`, `ended`";

    bool ReadRun(Field* f, uint32_t accountId, Hs::ArenaRun& out)
    {
        Hs::ArenaRun run;
        run.id = f[0].GetUInt32();
        run.accountId = accountId;
        if (!Hs::ParseArenaState(TextOf(f[1]), run.state) || !Hs::ParseArenaState(TextOf(f[2]), run.repairFrom) ||
            !Hs::ParseArenaPay(TextOf(f[9]), run.terms.pay) ||
            !Hs::ParseArenaRarityWeights(TextOf(f[13]), run.terms.rarityWeights))
            return false;
        run.deckClass = TextOf(f[3]);
        run.wins = f[4].GetUInt32();
        run.draws = f[5].GetUInt32();
        run.losses = f[6].GetUInt32();
        run.matchNo = f[7].GetUInt32();
        run.resultNo = f[8].GetUInt32();
        run.terms.payAmount = f[10].GetUInt32();
        run.terms.basePacks = f[11].GetUInt32();
        run.terms.bonusCoins = f[12].GetUInt32();
        run.terms.rulesVersion = f[14].GetUInt32();
        run.terms.datasetHash = TextOf(f[15]);
        run.payingGuid = f[16].GetUInt32();
        run.slotPeriod = f[17].GetUInt64();
        run.entryConfirmed = f[18].GetUInt32() != 0;
        run.controllerGuid = f[19].GetUInt32();
        run.endedGuid = f[20].GetUInt32();
        run.retired = f[21].GetUInt32() != 0;
        run.prize.packs = f[22].GetUInt32();
        run.prize.coins = f[23].GetUInt32();
        run.prize.specialChoice = f[24].GetUInt32() != 0;
        run.specialPending = f[25].GetUInt32() != 0;
        run.specialCard = TextOf(f[26]);
        run.specialCopies = f[27].GetUInt32();
        run.created = f[28].GetUInt64();
        run.ended = f[29].GetUInt64();
        out = std::move(run);
        return true;
    }

    // The columns of one pick row: the run id (or an expression for it), the pick number, the offer, the answer.
    std::string PickColumns(std::string const& runId, std::size_t no, Hs::ArenaPick const& pick)
    {
        return runId + ", " + std::to_string(no) + ", '" + Escaped(Hs::EncodeArenaIds(pick.offer), Hs::kArenaOfferBytes) +
            "', '" + Escaped(pick.chosen, Hs::kMaxCardIdLen) + "'";
    }
}

HsArenaStoreJoin::HsArenaStoreJoin(uint32_t payingGuid)
{
    ++t_joinDepth;
    t_joinPayer = payingGuid;
}

HsArenaStoreJoin::~HsArenaStoreJoin()
{
    if (--t_joinDepth == 0)
        t_joinPayer = 0;
}

bool HsArenaDbStorage::LoadLatestRun(uint32_t accountId, bool& found, Hs::ArenaRun& out)
{
    // A plain SELECT returns null both for "no rows" and for a failed query: a scalar head row (always one row)
    // tells them apart, as in HsCollectionDbStorage::Load.
    std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
        "SELECT (SELECT COUNT(*) FROM `hearthstone_arena_run` WHERE `account_id` = %u)", accountId));
    if (!head)
    {
        sLog.outError("Hearthstone: cannot read the arena run of account %u "
            "(character migration 0010_hearthstone_arena.sql not applied, or database error)", accountId);
        return false;
    }
    found = false;
    if (head->Fetch()[0].GetUInt64() == 0)
        return true;

    std::unique_ptr<QueryResult> row(CharacterDatabase.PQuery(
        "SELECT %s FROM `hearthstone_arena_run` WHERE `account_id` = %u ORDER BY `run_id` DESC LIMIT 1", kRunColumns,
        accountId));
    Hs::ArenaRun run;
    if (!row || !ReadRun(row->Fetch(), accountId, run))
    {
        sLog.outError("Hearthstone: cannot read the newest arena run row of account %u", accountId);
        return false;
    }

    // Pick 0 (the class offer) exists from the moment the run does: no rows is a failure here.
    std::unique_ptr<QueryResult> picks(CharacterDatabase.PQuery(
        "SELECT `pick_no`, `offer`, `chosen` FROM `hearthstone_arena_pick` WHERE `run_id` = %u ORDER BY `pick_no`", run.id));
    if (!picks)
    {
        sLog.outError("Hearthstone: arena run %u of account %u has no picks", run.id, accountId);
        return false;
    }
    do
    {
        Field* f = picks->Fetch();
        Hs::ArenaPick pick;
        if (f[0].GetUInt32() != run.picks.size() ||
            !Hs::DecodeArenaIds(TextOf(f[1]), Hs::kArenaOfferSize, pick.offer))
        {
            sLog.outError("Hearthstone: arena run %u of account %u has a malformed pick row", run.id, accountId);
            return false;
        }
        pick.chosen = TextOf(f[2]);
        run.picks.push_back(std::move(pick));
    } while (picks->NextRow());

    // The entry snapshot of the quest cards (migration 0012): no rows is the empty set, as for every older run. The
    // count tells "no rows" apart from a failed query, which a plain SELECT cannot.
    std::unique_ptr<QueryResult> questHead(CharacterDatabase.PQuery(
        "SELECT (SELECT COUNT(*) FROM `hearthstone_arena_quest` WHERE `run_id` = %u)", run.id));
    if (!questHead)
    {
        sLog.outError("Hearthstone: cannot read the quest snapshot of arena run %u of account %u", run.id, accountId);
        return false;
    }
    if (questHead->Fetch()[0].GetUInt64() != 0)
    {
        std::unique_ptr<QueryResult> quest(CharacterDatabase.PQuery(
            "SELECT `card_id` FROM `hearthstone_arena_quest` WHERE `run_id` = %u ORDER BY `card_id`", run.id));
        if (!quest)
        {
            sLog.outError("Hearthstone: cannot read the quest snapshot rows of arena run %u of account %u", run.id, accountId);
            return false;
        }
        do
            run.questCards.push_back(TextOf(quest->Fetch()[0]));
        while (quest->NextRow());
    }

    out = std::move(run);
    found = true;
    return true;
}

bool HsArenaCountGoodRuns(uint32_t accountId, uint32_t minWins, uint32_t& count)
{
    // COUNT(*) always returns one row, so a null result is a failed query (not "no rows").
    std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
        "SELECT COUNT(*) FROM `hearthstone_arena_run` WHERE `account_id` = %u AND `entry_confirmed` = 1 "
        "AND `pay_method` <> 'practice' AND `state` IN ('finished', 'retired', 'settled') AND `wins` >= %u",
        accountId, minWins));
    if (!head)
    {
        sLog.outError("Hearthstone: cannot count the arena runs of account %u with %u wins "
            "(character migration 0010_hearthstone_arena.sql not applied, or database error)", accountId, minWins);
        return false;
    }
    count = static_cast<uint32_t>(head->Fetch()[0].GetUInt64());
    return true;
}

bool HsArenaCountPaidEntries(uint32_t accountId, uint32_t& count)
{
    // The paid methods are the ones that use the daily slot (Hs::ArenaPayUsesDailySlot): gold and coins.
    std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
        "SELECT COUNT(*) FROM `hearthstone_arena_run` WHERE `account_id` = %u AND `entry_confirmed` = 1 "
        "AND `pay_method` IN ('gold', 'coins')",
        accountId));
    if (!head)
    {
        sLog.outError("Hearthstone: cannot count the paid arena entries of account %u "
            "(character migration 0010_hearthstone_arena.sql not applied, or database error)", accountId);
        return false;
    }
    count = static_cast<uint32_t>(head->Fetch()[0].GetUInt64());
    return true;
}

bool HsArenaDbStorage::IsSlotUsed(uint32_t accountId, uint64_t period, bool& used)
{
    std::unique_ptr<QueryResult> row(CharacterDatabase.PQuery(
        "SELECT (SELECT COUNT(*) FROM `hearthstone_arena_entry` WHERE `period_start` = %llu AND `account_id` = %u)",
        Wide(period), accountId));
    if (!row)
    {
        sLog.outError("Hearthstone: cannot read the arena entry slot of account %u", accountId);
        return false;
    }
    used = row->Fetch()[0].GetUInt64() != 0;
    return true;
}

Hs::ArenaStoreResult HsArenaDbStorage::CreateRun(Hs::ArenaRun& run)
{
    bool const slot = Hs::ArenaPayUsesDailySlot(run.terms.pay);
    // The unique keys are the rule; this read only names the reason without a failed statement in the log.
    std::unique_ptr<QueryResult> taken(CharacterDatabase.PQuery(
        "SELECT (SELECT COUNT(*) FROM `hearthstone_arena_run` WHERE `open_account` = %u), "
        "(SELECT COUNT(*) FROM `hearthstone_arena_entry` WHERE `period_start` = %llu AND `account_id` = %u)",
        run.accountId, Wide(run.slotPeriod), run.accountId));
    if (!taken)
        return Hs::ArenaStoreResult::Failed;
    if (taken->Fetch()[0].GetUInt64() != 0)
        return Hs::ArenaStoreResult::OpenRun;
    if (slot && taken->Fetch()[1].GetUInt64() != 0)
        return Hs::ArenaStoreResult::SlotTaken;

    if (!CharacterDatabase.BeginTransaction())
    {
        sLog.outError("Hearthstone: BeginTransaction failed for a new arena run of account %u", run.accountId);
        return Hs::ArenaStoreResult::Failed;
    }
    std::string const weights = Escaped(Hs::FormatArenaRarityWeights(run.terms.rarityWeights), 40);
    std::string const hash = Escaped(run.terms.datasetHash, 32);
    bool queued = CharacterDatabase.PExecute(
        "INSERT INTO `hearthstone_arena_run` (`account_id`, `open_account`, `state`, `repair_from`, `deck_class`, "
        "`pay_method`, `pay_amount`, `base_packs`, `bonus_coins`, `rarity_weights`, `rules_version`, `dataset_hash`, "
        "`paying_guid`, `slot_period`, `entry_confirmed`, `created`) VALUES (%u, %u, '%s', '%s', '', '%s', %u, %u, %u, "
        "'%s', %u, '%s', %u, %llu, %u, %llu)", run.accountId, run.accountId, Hs::ArenaStateCode(run.state),
        Hs::ArenaStateCode(run.repairFrom), Hs::ArenaPayCode(run.terms.pay), run.terms.payAmount, run.terms.basePacks,
        run.terms.bonusCoins, weights.c_str(), run.terms.rulesVersion, hash.c_str(), run.payingGuid, Wide(run.slotPeriod),
        run.entryConfirmed ? 1u : 0u, Wide(run.created));
    // The run id is the auto number of the INSERT above: LAST_INSERT_ID() of this connection, which the entry slot
    // and the picks (no auto number of their own) leave untouched.
    if (queued && slot)
        queued = CharacterDatabase.PExecute(
            "INSERT INTO `hearthstone_arena_entry` (`period_start`, `account_id`, `run_id`) SELECT %llu, %u, LAST_INSERT_ID()",
            Wide(run.slotPeriod), run.accountId);
    // The entry snapshot of the owned quest cards (migration 0012), in the same transaction as the run.
    if (queued && !run.questCards.empty())
    {
        std::string select;
        for (std::size_t i = 0; i < run.questCards.size(); ++i)
            select += (i ? " UNION ALL SELECT " : "SELECT ") + std::string("LAST_INSERT_ID(), '") +
                Escaped(run.questCards[i], 32) + "'";
        queued = CharacterDatabase.PExecute(
            "INSERT INTO `hearthstone_arena_quest` (`run_id`, `card_id`) %s", select.c_str());
    }
    if (queued && !run.picks.empty())
    {
        std::string select;
        for (std::size_t i = 0; i < run.picks.size(); ++i)
            select += (i ? " UNION ALL SELECT " : "SELECT ") + PickColumns("LAST_INSERT_ID()", i, run.picks[i]);
        queued = CharacterDatabase.PExecute(
            "INSERT INTO `hearthstone_arena_pick` (`run_id`, `pick_no`, `offer`, `chosen`) %s", select.c_str());
    }
    if (!FinishTransaction(queued, "entry reservation", 0))
    {
        // Either unique key fired between the read above and the commit (a concurrent entry), or the database failed.
        std::unique_ptr<QueryResult> now(CharacterDatabase.PQuery(
            "SELECT (SELECT COUNT(*) FROM `hearthstone_arena_run` WHERE `open_account` = %u), "
            "(SELECT COUNT(*) FROM `hearthstone_arena_entry` WHERE `period_start` = %llu AND `account_id` = %u)",
            run.accountId, Wide(run.slotPeriod), run.accountId));
        if (now && now->Fetch()[0].GetUInt64() != 0)
            return Hs::ArenaStoreResult::OpenRun;
        if (now && slot && now->Fetch()[1].GetUInt64() != 0)
            return Hs::ArenaStoreResult::SlotTaken;
        return Hs::ArenaStoreResult::Failed;
    }
    // The unique open_account key makes the committed run findable without a client-visible auto number.
    std::unique_ptr<QueryResult> id(CharacterDatabase.PQuery(
        "SELECT `run_id` FROM `hearthstone_arena_run` WHERE `open_account` = %u", run.accountId));
    if (!id)
    {
        sLog.outError("Hearthstone: the new arena run of account %u committed but cannot be read; a retry resumes it",
            run.accountId);
        return Hs::ArenaStoreResult::Failed;
    }
    run.id = id->Fetch()[0].GetUInt32();
    HsArenaMarkDirty(run.accountId);
    return Hs::ArenaStoreResult::Ok;
}

bool HsArenaDbStorage::SaveRun(Hs::ArenaRun const& run)
{
    bool const join = t_joinDepth > 0;
    if (join)
    {
        if (!CharacterDatabase.InTransaction())
        {
            sLog.outError("Hearthstone: arena run %u: a joined save without an open transaction", run.id);
            return false;
        }
    }
    else if (!CharacterDatabase.BeginTransaction())
    {
        sLog.outError("Hearthstone: BeginTransaction failed for arena run %u", run.id);
        return false;
    }

    std::string const open = run.Open() ? std::to_string(run.accountId) : std::string("NULL");
    std::string const hash = Escaped(run.terms.datasetHash, 32);
    std::string const card = Escaped(run.specialCard, Hs::kMaxCardIdLen);
    uint32_t const payer = join && t_joinPayer != 0 ? t_joinPayer : run.payingGuid;
    bool queued = CharacterDatabase.PExecute(
        "UPDATE `hearthstone_arena_run` SET `open_account` = %s, `state` = '%s', `repair_from` = '%s', "
        "`deck_class` = '%s', `wins` = %u, `draws` = %u, `losses` = %u, `match_no` = %u, `result_no` = %u, "
        "`rules_version` = %u, `dataset_hash` = '%s', `paying_guid` = %u, `entry_confirmed` = %u, "
        "`controller_guid` = %u, `ended_guid` = %u, `retired` = %u, `prize_packs` = %u, `prize_coins` = %u, "
        "`prize_special` = %u, `special_pending` = %u, `special_card` = '%s', `special_copies` = %u, `ended` = %llu "
        "WHERE `run_id` = %u AND `account_id` = %u", open.c_str(), Hs::ArenaStateCode(run.state),
        Hs::ArenaStateCode(run.repairFrom), Escaped(run.deckClass, 12).c_str(), run.wins, run.draws, run.losses,
        run.matchNo, run.resultNo, run.terms.rulesVersion, hash.c_str(), payer, run.entryConfirmed ? 1u : 0u,
        run.controllerGuid, run.endedGuid, run.retired ? 1u : 0u, run.prize.packs, run.prize.coins,
        run.prize.specialChoice ? 1u : 0u, run.specialPending ? 1u : 0u, card.c_str(), run.specialCopies, Wide(run.ended),
        run.id, run.accountId);
    if (queued)
    {
        // The picks only ever grow and change at their tail, but the contract is "replace": drop what the run no
        // longer has, then write every pick.
        queued = CharacterDatabase.PExecute(
            "DELETE FROM `hearthstone_arena_pick` WHERE `run_id` = %u AND `pick_no` >= %u", run.id,
            static_cast<uint32_t>(run.picks.size()));
    }
    if (queued && !run.picks.empty())
    {
        std::string values;
        for (std::size_t i = 0; i < run.picks.size(); ++i)
            values += (i ? ", (" : "(") + PickColumns(std::to_string(run.id), i, run.picks[i]) + ")";
        queued = CharacterDatabase.PExecute(
            "REPLACE INTO `hearthstone_arena_pick` (`run_id`, `pick_no`, `offer`, `chosen`) VALUES %s", values.c_str());
    }
    if (join)
    {
        // The caller commits (or rolls back) the transaction it opened.
        if (!queued)
            sLog.outError("Hearthstone: could not queue the joined save of arena run %u", run.id);
        else
            HsArenaMarkDirty(run.accountId);
        return queued;
    }
    bool const saved = FinishTransaction(queued, "save", run.id);
    if (saved)
        HsArenaMarkDirty(run.accountId); // the addon gets ARENA_UPD from the world update
    return saved;
}

bool HsArenaDbStorage::DeleteUnconfirmedRun(uint32_t accountId, uint32_t runId)
{
    if (!CharacterDatabase.BeginTransaction())
    {
        sLog.outError("Hearthstone: BeginTransaction failed to release arena run %u", runId);
        return false;
    }
    // Every delete is tied to the run still being unconfirmed, so a run whose entry committed after all is never
    // half removed: the slot and the picks go first (they join the run row), the run row last.
    bool queued = CharacterDatabase.PExecute(
        "DELETE e FROM `hearthstone_arena_entry` e JOIN `hearthstone_arena_run` r ON r.`run_id` = e.`run_id` "
        "WHERE e.`run_id` = %u AND r.`account_id` = %u AND r.`entry_confirmed` = 0", runId, accountId);
    queued = queued && CharacterDatabase.PExecute(
        "DELETE p FROM `hearthstone_arena_pick` p JOIN `hearthstone_arena_run` r ON r.`run_id` = p.`run_id` "
        "WHERE p.`run_id` = %u AND r.`account_id` = %u AND r.`entry_confirmed` = 0", runId, accountId);
    queued = queued && CharacterDatabase.PExecute(
        "DELETE q FROM `hearthstone_arena_quest` q JOIN `hearthstone_arena_run` r ON r.`run_id` = q.`run_id` "
        "WHERE q.`run_id` = %u AND r.`account_id` = %u AND r.`entry_confirmed` = 0", runId, accountId);
    queued = queued && CharacterDatabase.PExecute(
        "DELETE FROM `hearthstone_arena_run` WHERE `run_id` = %u AND `account_id` = %u AND `entry_confirmed` = 0", runId,
        accountId);
    if (!FinishTransaction(queued, "release", runId))
        return false;
    // Committed: the run must be gone unless it was confirmed meanwhile (then nothing was removed).
    std::unique_ptr<QueryResult> left(CharacterDatabase.PQuery(
        "SELECT (SELECT COUNT(*) FROM `hearthstone_arena_run` WHERE `run_id` = %u)", runId));
    return left && left->Fetch()[0].GetUInt64() == 0;
}

// ---- duels --------------------------------------------------------------------------------------------------------

bool HsArenaDbStorage::LoadOpponent(uint32_t runId, uint32_t matchNo, bool& found, Hs::ArenaOpponent& out)
{
    std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
        "SELECT (SELECT COUNT(*) FROM `hearthstone_arena_match` WHERE `run_id` = %u AND `match_no` = %u)", runId, matchNo));
    if (!head)
    {
        sLog.outError("Hearthstone: cannot read the arena duel %u of run %u "
            "(character migration 0010_hearthstone_arena.sql not applied, or database error)", matchNo, runId);
        return false;
    }
    found = false;
    if (head->Fetch()[0].GetUInt64() == 0)
        return true;
    std::unique_ptr<QueryResult> row(CharacterDatabase.PQuery(
        "SELECT `opponent_class`, `opponent_name`, `opponent_deck`, `seed`, `timer_ms`, `rules_version`, `dataset_hash`, "
        "`started_at`, `result` FROM `hearthstone_arena_match` WHERE `run_id` = %u AND `match_no` = %u", runId, matchNo));
    Hs::ArenaOpponent opponent;
    if (!row)
    {
        sLog.outError("Hearthstone: cannot read the arena duel %u of run %u", matchNo, runId);
        return false;
    }
    Field* f = row->Fetch();
    opponent.runId = runId;
    opponent.matchNo = matchNo;
    opponent.deckClass = TextOf(f[0]);
    opponent.name = TextOf(f[1]);
    // A deck that does not decode stays empty: ArenaOpponentOk refuses it, and the run goes to repair.
    Hs::DecodeArenaIds(TextOf(f[2]), Hs::kDeckSize, opponent.deck);
    opponent.seed = f[3].GetUInt32();
    opponent.timerMs = f[4].GetUInt32();
    opponent.rulesVersion = static_cast<int>(f[5].GetUInt32());
    opponent.datasetHash = TextOf(f[6]);
    opponent.startedAt = static_cast<int64_t>(f[7].GetUInt64());
    opponent.result = TextOf(f[8]);
    out = std::move(opponent);
    found = true;
    return true;
}

bool HsArenaDbStorage::SaveOpponent(Hs::ArenaOpponent const& o)
{
    // Synchronous: the opponent is stored before the duel starts, so a relog or a crash never draws another one.
    std::string const deck = Escaped(Hs::EncodeArenaIds(o.deck), Hs::kArenaOpponentDeckBytes);
    std::string const name = Escaped(o.name, Hs::kArenaOpponentNameBytes);
    std::string const hash = Escaped(o.datasetHash, 32);
    return CharacterDatabase.DirectPExecute(
        "INSERT INTO `hearthstone_arena_match` (`run_id`, `match_no`, `opponent_class`, `opponent_name`, `opponent_deck`, "
        "`seed`, `timer_ms`, `rules_version`, `dataset_hash`, `started_at`, `result`) VALUES (%u, %u, '%s', '%s', '%s', %u, "
        "%u, %u, '%s', %llu, '')", o.runId, o.matchNo, Escaped(o.deckClass, 12).c_str(), name.c_str(), deck.c_str(),
        o.seed, o.timerMs, static_cast<uint32_t>(o.rulesVersion), hash.c_str(), Wide(static_cast<uint64_t>(o.startedAt)));
}

bool HsArenaDbStorage::SetResult(uint32_t runId, uint32_t matchNo, char const* result)
{
    return CharacterDatabase.DirectPExecute(
        "UPDATE `hearthstone_arena_match` SET `result` = '%s' WHERE `run_id` = %u AND `match_no` = %u", result, runId,
        matchNo);
}

bool HsArenaDbStorage::SetTimer(uint32_t runId, uint32_t matchNo, uint32_t timerMs)
{
    return CharacterDatabase.DirectPExecute(
        "UPDATE `hearthstone_arena_match` SET `timer_ms` = %u WHERE `run_id` = %u AND `match_no` = %u", timerMs, runId,
        matchNo);
}

bool HsArenaDbStorage::LoadActions(uint32_t runId, uint32_t matchNo, std::vector<std::string>& out)
{
    std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
        "SELECT (SELECT COUNT(*) FROM `hearthstone_arena_action` WHERE `run_id` = %u AND `match_no` = %u)", runId, matchNo));
    if (!head)
    {
        sLog.outError("Hearthstone: cannot read the replay log of arena duel %u of run %u", matchNo, runId);
        return false;
    }
    out.clear();
    uint64_t const count = head->Fetch()[0].GetUInt64();
    if (count == 0)
        return true;
    std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery(
        "SELECT `idx`, `action` FROM `hearthstone_arena_action` WHERE `run_id` = %u AND `match_no` = %u ORDER BY `idx`",
        runId, matchNo));
    if (!rows)
    {
        sLog.outError("Hearthstone: cannot read the replay log rows of arena duel %u of run %u", matchNo, runId);
        return false;
    }
    do
    {
        Field* f = rows->Fetch();
        if (f[0].GetUInt32() != out.size())
        {
            sLog.outError("Hearthstone: the replay log of arena duel %u of run %u has a gap at %u", matchNo, runId,
                static_cast<uint32_t>(out.size()));
            out.clear();
            return false;
        }
        out.push_back(TextOf(f[1]));
    } while (rows->NextRow());
    return true;
}

bool HsArenaDbStorage::ResetActions(uint32_t runId, uint32_t matchNo)
{
    return CharacterDatabase.DirectPExecute(
        "DELETE FROM `hearthstone_arena_action` WHERE `run_id` = %u AND `match_no` = %u", runId, matchNo);
}

bool HsArenaDbStorage::CountActions(uint32_t runId, uint32_t matchNo, uint32_t& count)
{
    std::unique_ptr<QueryResult> row(CharacterDatabase.PQuery(
        "SELECT (SELECT COUNT(*) FROM `hearthstone_arena_action` WHERE `run_id` = %u AND `match_no` = %u)", runId, matchNo));
    if (!row)
        return false;
    count = static_cast<uint32_t>(row->Fetch()[0].GetUInt64());
    return true;
}

void HsArenaDbStorage::SetActiveMatch(uint32_t runId, uint32_t matchNo)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_active[runId] = matchNo;
}

void HsArenaDbStorage::ClearActiveMatch(uint32_t runId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_active.erase(runId);
}

bool HsArenaDbStorage::ActiveMatch(uint32_t runId, uint32_t& matchNo)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto const it = m_active.find(runId);
    if (it == m_active.end())
        return false;
    matchNo = it->second;
    return true;
}

std::size_t HsArenaDbStorage::ActiveCount()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_active.size();
}

bool HsArenaDbStorage::RecoverDuels(uint32_t& count)
{
    count = 0;
    std::unique_ptr<QueryResult> row(CharacterDatabase.PQuery(
        "SELECT (SELECT COUNT(*) FROM `hearthstone_arena_run` WHERE `state` = '%s')",
        Hs::ArenaStateCode(Hs::ArenaState::InDuel)));
    if (!row)
        return false;
    count = static_cast<uint32_t>(row->Fetch()[0].GetUInt64());
    if (count == 0)
        return true;
    return CharacterDatabase.DirectPExecute(
        "UPDATE `hearthstone_arena_run` SET `state` = '%s' WHERE `state` = '%s'",
        Hs::ArenaStateCode(Hs::ArenaState::Paused), Hs::ArenaStateCode(Hs::ArenaState::InDuel));
}

// ---- the duel sink --------------------------------------------------------------------------------------------------

bool HsArenaDbStorage::StoreActions(uint32_t guid, uint64_t runId, uint32_t first,
                                    std::vector<Hs::LoggedAction> const& actions)
{
    if (actions.empty())
        return true;
    uint32_t matchNo = 0;
    if (!ActiveMatch(static_cast<uint32_t>(runId), matchNo))
    {
        sLog.outError("Hearthstone: arena actions of guid %u for run %llu, but the run has no active duel", guid, Wide(runId));
        return false;
    }
    // One statement for the whole batch: it is stored completely or not at all, and it has committed when it returns.
    std::string values;
    for (std::size_t i = 0; i < actions.size(); ++i)
    {
        std::string const text = Hs::EncodeAction(actions[i]);
        if (text.empty() || text.size() > Hs::kArenaActionBytes)
        {
            sLog.outError("Hearthstone: an arena action of run %llu cannot be stored", Wide(runId));
            return false;
        }
        values += (i ? ", (" : "(") + std::to_string(static_cast<uint32_t>(runId)) + ", " + std::to_string(matchNo) +
            ", " + std::to_string(first + static_cast<uint32_t>(i)) + ", '" + Escaped(text, Hs::kArenaActionBytes) + "')";
    }
    if (!CharacterDatabase.DirectPExecute(
        "INSERT INTO `hearthstone_arena_action` (`run_id`, `match_no`, `idx`, `action`) VALUES %s", values.c_str()))
    {
        sLog.outError("Hearthstone: could not store %u arena action(s) from #%u of duel %u of run %llu (guid %u)",
            static_cast<uint32_t>(actions.size()), first, matchNo, Wide(runId), guid);
        return false;
    }
    return true;
}

void HsArenaDbStorage::StoreSuspended(Hs::SuspendedDuel const& duel, char const* why)
{
    uint32_t const runId = static_cast<uint32_t>(duel.params.arenaRunId);
    uint32_t const accountId = duel.params.meta.accountId;
    uint32_t matchNo = 0;
    if (!ActiveMatch(runId, matchNo))
    {
        sLog.outError("Hearthstone: arena duel of run %u suspended (%s), but the run has no active duel", runId, why);
        return;
    }
    // A suspension because a store failed cut the log to what the player was shown: rows past it (a batch whose store
    // reported a failure after all) go, so the stored log is exactly that log. Any other suspension keeps every row.
    uint32_t const kept = static_cast<uint32_t>(duel.log.size());
    if (std::string(why) == "store" && !CharacterDatabase.DirectPExecute(
        "DELETE FROM `hearthstone_arena_action` WHERE `run_id` = %u AND `match_no` = %u AND `idx` >= %u", runId, matchNo, kept))
        sLog.outError("Hearthstone: could not trim the replay log of arena duel %u of run %u", matchNo, runId);
    uint32_t stored = 0;
    if (!CountActions(runId, matchNo, stored) || stored != kept)
        sLog.outError("Hearthstone: arena duel %u of run %u: %u action(s) in the log, %u stored", matchNo, runId, kept, stored);
    if (!SetTimer(runId, matchNo, duel.timerMs))
        sLog.outError("Hearthstone: could not store the turn timer of arena duel %u of run %u", matchNo, runId);
    Hs::ArenaOutcome const paused = HsArena().PauseDuel(accountId, runId);
    if (!paused.ok)
        sLog.outError("Hearthstone: arena run %u could not be paused (%s)", runId, paused.why.c_str());
    ClearActiveMatch(runId);
    sLog.outString("Hearthstone: arena run %u paused (%s): duel %u, %u action(s) stored, timer %u ms", runId, why, matchNo,
        stored, duel.timerMs);
}

// ---- process-wide ---------------------------------------------------------------------------------------------------

HsArenaDbStorage& HsArenaStore()
{
    static HsArenaDbStorage storage;
    return storage;
}

Hs::ArenaService& HsArena()
{
    static Hs::ArenaService service(HsArenaStore());
    return service;
}

bool CheckHsArenaTables()
{
    struct Table { char const* name; };
    Table const tables[] = {
        { "hearthstone_arena_run" },
        { "hearthstone_arena_entry" },
        { "hearthstone_arena_pick" },
        { "hearthstone_arena_match" },
        { "hearthstone_arena_action" },
        { "hearthstone_arena_milestone" },
        { "hearthstone_arena_op" },
        { "hearthstone_arena_payout" },        // 0011 (settlement)
        { "hearthstone_arena_coin_delivery" }, // 0011 (settlement)
        { "hearthstone_arena_quest" },         // 0012 (quest snapshot)
    };
    bool all = true;
    for (Table const& table : tables)
    {
        std::unique_ptr<QueryResult> found(CharacterDatabase.PQuery("SHOW TABLES LIKE '%s'", table.name));
        if (!found)
        {
            all = false;
            sLog.outError("Hearthstone: table %s is missing in the character database "
                "(migrations 0010/0011_hearthstone_arena*.sql not applied? check Database.AutoUpdate.*): the draft arena "
                "is unavailable", table.name);
        }
    }
    return all;
}
