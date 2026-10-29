#include "HsLadderDuelStore.h"

#include "Database/DatabaseEnv.h"
#include "Log.h"

#include "HsLadderStore.h"
#include "HsLadderTable.h"

#include <memory>
#include <utility>

namespace HL = Hs::Ladder;

namespace
{
    std::string EscapeText(std::string const& text)
    {
        std::string s = text;
        CharacterDatabase.escape_string(s);
        return s;
    }

    std::vector<std::string> CellsOf(Field* f, std::size_t n)
    {
        std::vector<std::string> cells;
        for (std::size_t i = 0; i < n; ++i)
            cells.push_back(f[i].IsNULL() ? std::string() : f[i].GetCppString());
        return cells;
    }

    unsigned long long Wide(uint64_t v)
    {
        return static_cast<unsigned long long>(v);
    }
}

bool CheckHsLadderDuelTables()
{
    char const* const tables[] = { HL::kTableDuel, HL::kTableDuelAction, HL::kTableQueue };
    bool all = true;
    for (char const* table : tables)
    {
        std::unique_ptr<QueryResult> found(CharacterDatabase.PQuery("SHOW TABLES LIKE '%s'", table));
        if (!found)
        {
            all = false;
            sLog.outError("Hearthstone: table %s is missing in the character database "
                "(migration 0017_hearthstone_ladder_duel.sql not applied? check Database.AutoUpdate.*): ranked ladder duels "
                "are unavailable", table);
        }
    }
    return all;
}

HsLadderDuelStorage& HsLadderDuels()
{
    static HsLadderDuelStorage storage;
    return storage;
}

bool HsLadderDuelStorage::Startup()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_ready = false;
        m_byAccount.clear();
        m_nextId = 1;
    }
    if (!CheckHsLadderDuelTables())
        return false;
    std::vector<HL::DuelRow> duels;
    if (!LoadAll(duels))
        return false;
    uint32_t paused = 0;
    std::vector<std::string> statements;
    for (HL::DuelRow const& row : duels)
        if (row.state != HL::kDuelPaused)
        {
            ++paused;
            statements.push_back(HL::DuelPauseSql(row.id, 0, false, 0)[0]);
        }
    // A duel that was active when the server stopped has no match any more: it is paused. Its remaining turn timer is gone
    // with the process, so the turn starts afresh when it is resumed.
    if (!statements.empty() && !HsLadderStore().Run(statements, "duel recovery"))
        return false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (HL::DuelRow const& row : duels)
        {
            m_byAccount[row.account] = row.id;
            m_nextId = std::max(m_nextId, row.id + 1);
        }
        m_ready = true;
    }
    sLog.outString("Hearthstone: ladder duels ready: %u stored duel(s), %u of them paused by this start", static_cast<uint32_t>(duels.size()),
        paused);
    return true;
}

bool HsLadderDuelStorage::Ready()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_ready;
}

bool HsLadderDuelStorage::Has(uint32_t accountId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_byAccount.count(accountId) != 0;
}

bool HsLadderDuelStorage::LoadAll(std::vector<HL::DuelRow>& out)
{
    out.clear();
    std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery("SELECT (SELECT COUNT(*) FROM `%s`)", HL::kTableDuel));
    if (!head)
    {
        sLog.outError("Hearthstone: cannot read the ladder duels");
        return false;
    }
    if (head->Fetch()[0].GetUInt64() == 0)
        return true;
    std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery("SELECT %s FROM `%s` ORDER BY `duel_id`", HL::kDuelColumns,
        HL::kTableDuel));
    if (!rows)
    {
        sLog.outError("Hearthstone: cannot read the ladder duel rows");
        return false;
    }
    do
    {
        HL::DuelRow row;
        if (!HL::DuelRowFromCells(CellsOf(rows->Fetch(), HL::kDuelColumnCount), row))
        {
            sLog.outError("Hearthstone: a ladder duel row is malformed (after duel %llu)", Wide(out.empty() ? 0 : out.back().id));
            out.clear();
            return false;
        }
        out.push_back(std::move(row));
    } while (rows->NextRow());
    return true;
}

bool HsLadderDuelStorage::Find(uint32_t accountId, bool& found, HL::DuelRow& out)
{
    found = false;
    std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery("SELECT (SELECT COUNT(*) FROM `%s` WHERE `account_id` = %u)",
        HL::kTableDuel, accountId));
    if (!head)
    {
        sLog.outError("Hearthstone: cannot read the ladder duel of account %u", accountId);
        return false;
    }
    if (head->Fetch()[0].GetUInt64() == 0)
        return true;
    std::unique_ptr<QueryResult> row(CharacterDatabase.PQuery("SELECT %s FROM `%s` WHERE `account_id` = %u", HL::kDuelColumns,
        HL::kTableDuel, accountId));
    HL::DuelRow parsed;
    if (!row || !HL::DuelRowFromCells(CellsOf(row->Fetch(), HL::kDuelColumnCount), parsed))
    {
        sLog.outError("Hearthstone: the ladder duel row of account %u is malformed", accountId);
        return false;
    }
    out = std::move(parsed);
    found = true;
    return true;
}

bool HsLadderDuelStorage::FindById(uint64_t duelId, bool& found, HL::DuelRow& out)
{
    found = false;
    std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery("SELECT (SELECT COUNT(*) FROM `%s` WHERE `duel_id` = %llu)",
        HL::kTableDuel, Wide(duelId)));
    if (!head)
    {
        sLog.outError("Hearthstone: cannot read the ladder duel %llu", Wide(duelId));
        return false;
    }
    if (head->Fetch()[0].GetUInt64() == 0)
        return true;
    std::unique_ptr<QueryResult> row(CharacterDatabase.PQuery("SELECT %s FROM `%s` WHERE `duel_id` = %llu", HL::kDuelColumns,
        HL::kTableDuel, Wide(duelId)));
    HL::DuelRow parsed;
    if (!row || !HL::DuelRowFromCells(CellsOf(row->Fetch(), HL::kDuelColumnCount), parsed))
    {
        sLog.outError("Hearthstone: the ladder duel row %llu is malformed", Wide(duelId));
        return false;
    }
    out = std::move(parsed);
    found = true;
    return true;
}

bool HsLadderDuelStorage::LoadLog(uint64_t duelId, std::vector<std::string>& rows)
{
    rows.clear();
    std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery("SELECT (SELECT COUNT(*) FROM `%s` WHERE `duel_id` = %llu)",
        HL::kTableDuelAction, Wide(duelId)));
    if (!head)
    {
        sLog.outError("Hearthstone: cannot read the replay log of ladder duel %llu", Wide(duelId));
        return false;
    }
    if (head->Fetch()[0].GetUInt64() == 0)
        return true;
    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery("SELECT `action` FROM `%s` WHERE `duel_id` = %llu ORDER BY `idx`",
        HL::kTableDuelAction, Wide(duelId)));
    if (!result)
    {
        sLog.outError("Hearthstone: cannot read the replay log rows of ladder duel %llu", Wide(duelId));
        return false;
    }
    do
        rows.push_back(result->Fetch()[0].GetCppString());
    while (result->NextRow());
    return true;
}

bool HsLadderDuelStorage::CountLog(uint64_t duelId, uint32_t& count)
{
    count = 0;
    std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery("SELECT (SELECT COUNT(*) FROM `%s` WHERE `duel_id` = %llu)",
        HL::kTableDuelAction, Wide(duelId)));
    if (!head)
        return false;
    count = static_cast<uint32_t>(head->Fetch()[0].GetUInt64());
    return true;
}

bool HsLadderDuelStorage::Begin(HL::DuelRow& row)
{
    uint64_t id = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_ready || m_byAccount.count(row.account))
            return false;
        id = m_nextId++;
        // Reserved before the write: a second start of the account (another thread) now finds it taken.
        m_byAccount[row.account] = id;
    }
    row.id = id;
    row.state = HL::kDuelActive;
    if (!HsLadderStore().Run({ HL::DuelInsertSql(row, EscapeText) }, "duel start"))
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_byAccount.erase(row.account);
        return false;
    }
    return true;
}

bool HsLadderDuelStorage::MarkActive(uint64_t duelId, uint32_t guid)
{
    return HsLadderStore().Run({ HL::DuelActiveSql(duelId, guid) }, "duel resume");
}

bool HsLadderDuelStorage::Abandon(uint64_t duelId)
{
    uint32_t account = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto const& entry : m_byAccount)
            if (entry.second == duelId)
                account = entry.first;
    }
    if (!HsLadderStore().Run(HL::DuelDeleteSql(duelId), "duel abandon"))
        return false;
    if (account != 0)
        Settled(account);
    return true;
}

void HsLadderDuelStorage::Settled(uint32_t accountId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_byAccount.erase(accountId);
}

bool HsLadderDuelStorage::Reset()
{
    if (!HsLadderStore().Run(HL::DuelResetSql(), "duel reset"))
        return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_byAccount.clear();
    return true;
}

std::vector<std::string> LadderQueueStatements(std::vector<HL::QueueRow> const& rows)
{
    return HL::QueueSnapshotSql(rows, EscapeText);
}

bool HsLadderDuelStorage::LoadQueue(std::vector<HL::QueueRow>& out)
{
    out.clear();
    std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery("SELECT (SELECT COUNT(*) FROM `%s`)", HL::kTableQueue));
    if (!head)
    {
        sLog.outError("Hearthstone: cannot read the queued ladder work");
        return false;
    }
    if (head->Fetch()[0].GetUInt64() == 0)
        return true;
    std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery("SELECT %s FROM `%s` ORDER BY `seq`", HL::kQueueColumns,
        HL::kTableQueue));
    if (!rows)
    {
        sLog.outError("Hearthstone: cannot read the queued ladder work rows");
        return false;
    }
    do
    {
        HL::QueueRow row;
        if (!HL::QueueRowFromCells(CellsOf(rows->Fetch(), HL::kQueueColumnCount), row))
        {
            sLog.outError("Hearthstone: a queued ladder work row is malformed");
            out.clear();
            return false;
        }
        out.push_back(row);
    } while (rows->NextRow());
    return true;
}

// ---- the duel sink ---------------------------------------------------------------------------------------------------

bool HsLadderDuelStorage::StoreActions(uint32_t guid, uint64_t runId, uint32_t first, std::vector<Hs::LoggedAction> const& actions)
{
    if (actions.empty())
        return true;
    if (!HL::IsLadderRun(runId))
    {
        sLog.outError("Hearthstone: ladder duel actions of guid %u for run %llu, which is no ladder duel", guid, Wide(runId));
        return false;
    }
    uint64_t const duelId = HL::DuelIdOfRun(runId);
    bool ok = false;
    std::string const sql = HL::DuelActionsInsertSql(duelId, first, actions, ok);
    if (!ok)
    {
        sLog.outError("Hearthstone: an action of ladder duel %llu cannot be stored", Wide(duelId));
        return false;
    }
    // One statement for the whole batch: stored completely or not at all, and committed when it returns.
    if (!CharacterDatabase.DirectPExecute("%s", sql.c_str()))
    {
        sLog.outError("Hearthstone: could not store %u action(s) from #%u of ladder duel %llu (guid %u)",
            static_cast<uint32_t>(actions.size()), first, Wide(duelId), guid);
        return false;
    }
    return true;
}

void HsLadderDuelStorage::StoreSuspended(Hs::SuspendedDuel const& duel, char const* why)
{
    uint64_t const duelId = HL::DuelIdOfRun(duel.params.arenaRunId);
    uint32_t const kept = static_cast<uint32_t>(duel.log.size());
    // A suspension because a store failed cut the log to what the player was shown: rows past it (a batch whose store
    // reported a failure after all) go. Any other suspension keeps every row.
    bool const trim = std::string(why) == "store";
    if (!HsLadderStore().Run(HL::DuelPauseSql(duelId, duel.timerMs, trim, kept), "duel pause"))
        sLog.outError("Hearthstone: ladder duel %llu could not be marked paused (%s); it is paused by the next start", Wide(duelId), why);
    uint32_t stored = 0;
    if (!CountLog(duelId, stored) || stored != kept)
        sLog.outError("Hearthstone: ladder duel %llu: %u action(s) in the log, %u stored", Wide(duelId), kept, stored);
    sLog.outString("Hearthstone: ladder duel %llu paused (%s): %u action(s) stored, timer %u ms", Wide(duelId), why, stored,
        duel.timerMs);
    HsLadderMatchSuspended(duel.params.guid, why);
}
