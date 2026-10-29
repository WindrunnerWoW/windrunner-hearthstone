#include "HsLadderStore.h"

#include "Database/DatabaseEnv.h"
#include "Log.h"

#include "ladder/HsLadderDuel.h"
#include "match/HsMatch.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    std::string EscapeText(std::string const& text)
    {
        std::string s = text;
        CharacterDatabase.escape_string(s);
        return s;
    }

    // The text of the first n fields of the current row (NULL is the empty text).
    std::vector<std::string> CellsOf(Field* f, std::size_t n)
    {
        std::vector<std::string> cells;
        for (std::size_t i = 0; i < n; ++i)
            cells.push_back(f[i].IsNULL() ? std::string() : f[i].GetCppString());
        return cells;
    }

    uint32_t RulesVersion()
    {
        return static_cast<uint32_t>(Hs::Match::kRulesVersion);
    }
}

bool HsLadderDbStore::Commit(std::vector<std::string> const& statements, char const* what)
{
    if (!CharacterDatabase.BeginTransaction())
    {
        sLog.outError("Hearthstone: BeginTransaction failed for the ladder %s", what);
        return false;
    }
    for (std::string const& sql : statements)
    {
        if (!CharacterDatabase.Execute(sql.c_str()))
        {
            CharacterDatabase.RollbackTransaction();
            sLog.outError("Hearthstone: could not queue a statement of the ladder %s", what);
            return false;
        }
    }
    if (!CharacterDatabase.CommitTransactionDirect())
    {
        sLog.outError("Hearthstone: commit failed for the ladder %s (character migration 0015_hearthstone_ladder.sql "
            "applied? see the database log)", what);
        return false;
    }
    return true;
}

bool HsLadderDbStore::Load(bool& found, Hs::Ladder::LadderState& out, uint32_t& rulesVersion)
{
    found = false;
    // A plain SELECT returns null both for "no rows" and for a failed query: a scalar head row (always one row) tells
    // them apart, as in HsArenaDbStorage::LoadLatestRun.
    std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
        "SELECT (SELECT COUNT(*) FROM `hearthstone_ladder`), (SELECT COUNT(*) FROM `hearthstone_ladder_entrant`), "
        "(SELECT COUNT(*) FROM `hearthstone_ladder_vs`)"));
    if (!head)
    {
        sLog.outError("Hearthstone: cannot read the ladder "
            "(character migration 0015_hearthstone_ladder.sql not applied, or database error)");
        return false;
    }
    uint64_t const ladderRows = head->Fetch()[0].GetUInt64();
    uint64_t const entrantRows = head->Fetch()[1].GetUInt64();
    uint64_t const vsRows = head->Fetch()[2].GetUInt64();
    if (ladderRows == 0)
    {
        if (entrantRows != 0 || vsRows != 0)
            sLog.outError("Hearthstone: the ladder tables hold %llu entrant(s) and %llu record(s) but no ladder row; "
                "they are deleted when a new ladder is created", static_cast<unsigned long long>(entrantRows),
                static_cast<unsigned long long>(vsRows));
        return true;
    }

    std::unique_ptr<QueryResult> ladder(CharacterDatabase.PQuery(
        "SELECT %s FROM `hearthstone_ladder` WHERE `ladder_id` = 1", Hs::Ladder::kLadderColumns));
    Hs::Ladder::LadderRow ladderRow;
    if (!ladder || !Hs::Ladder::LadderRowFromCells(CellsOf(ladder->Fetch(), Hs::Ladder::kLadderColumnCount), ladderRow))
    {
        sLog.outError("Hearthstone: cannot read the ladder row");
        return false;
    }

    std::vector<Hs::Ladder::EntrantRow> entrants;
    if (entrantRows != 0)
    {
        std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery(
            "SELECT %s FROM `hearthstone_ladder_entrant` ORDER BY `entrant_id`", Hs::Ladder::kEntrantColumns));
        if (!rows)
        {
            sLog.outError("Hearthstone: cannot read the ladder entrants");
            return false;
        }
        do
        {
            Hs::Ladder::EntrantRow row;
            if (!Hs::Ladder::EntrantRowFromCells(CellsOf(rows->Fetch(), Hs::Ladder::kEntrantColumnCount), row))
            {
                sLog.outError("Hearthstone: a ladder entrant row is malformed (after entrant %llu)",
                    static_cast<unsigned long long>(entrants.empty() ? 0 : entrants.back().id));
                return false;
            }
            entrants.push_back(std::move(row));
        } while (rows->NextRow());
    }

    std::vector<Hs::Ladder::VsRow> vs;
    if (vsRows != 0)
    {
        std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery(
            "SELECT %s FROM `hearthstone_ladder_vs` ORDER BY `player_id`, `npc_id`", Hs::Ladder::kVsColumns));
        if (!rows)
        {
            sLog.outError("Hearthstone: cannot read the ladder records");
            return false;
        }
        do
        {
            Hs::Ladder::VsRow row;
            if (!Hs::Ladder::VsRowFromCells(CellsOf(rows->Fetch(), Hs::Ladder::kVsColumnCount), row))
            {
                sLog.outError("Hearthstone: a ladder record row is malformed");
                return false;
            }
            vs.push_back(row);
        } while (rows->NextRow());
    }

    std::string error;
    Hs::Ladder::LadderState state;
    if (!Hs::Ladder::BuildState(ladderRow, entrants, vs, state, error))
    {
        sLog.outError("Hearthstone: the stored ladder is not valid: %s", error.c_str());
        return false;
    }
    out = std::move(state);
    rulesVersion = ladderRow.rulesVersion;
    found = true;
    return true;
}

bool HsLadderDbStore::SaveRoster(Hs::Ladder::LadderState const& state, std::vector<Hs::Ladder::EntrantId> const& changed)
{
    return Commit(Hs::Ladder::RosterSaveSql(state, changed, RulesVersion(), EscapeText), "roster change");
}

bool HsLadderDbStore::SaveRound(Hs::Ladder::LadderState const& state, Hs::Ladder::RoundPlan const& plan,
                                std::vector<Hs::Ladder::GameResult> const& results, Hs::Ladder::RoundReport const& report,
                                std::vector<std::string> const& extra)
{
    std::vector<std::string> statements = Hs::Ladder::RoundSaveSql(state, plan, results, report, RulesVersion(), EscapeText);
    statements.insert(statements.end(), extra.begin(), extra.end());
    return Commit(statements, "round");
}

bool HsLadderDbStore::SavePlacement(Hs::Ladder::LadderState const& state)
{
    return Commit(Hs::Ladder::PlacementSaveSql(state, RulesVersion(), EscapeText), "placement");
}

bool HsLadderDbStore::SavePlayerResult(Hs::Ladder::LadderState const& state, Hs::Ladder::EntrantId player,
                                       Hs::Ladder::EntrantId npc, std::vector<Hs::Ladder::EntrantId> const& pushedOut,
                                       std::vector<std::string> const& extra)
{
    std::vector<std::string> statements =
        Hs::Ladder::PlayerResultSaveSql(state, player, npc, pushedOut, RulesVersion(), EscapeText);
    statements.insert(statements.end(), extra.begin(), extra.end());
    return Commit(statements, "player result");
}

bool HsLadderDbStore::LoadOffers(uint32_t accountId, bool& found, Hs::Ladder::OfferSet& out)
{
    found = false;
    std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
        "SELECT (SELECT COUNT(*) FROM `hearthstone_ladder_offer_state` WHERE `account_id` = %u), "
        "(SELECT COUNT(*) FROM `hearthstone_ladder_offer` WHERE `account_id` = %u)", accountId, accountId));
    if (!head)
    {
        sLog.outError("Hearthstone: cannot read the ladder offers of account %u "
            "(character migration 0016_hearthstone_ladder_offer.sql not applied, or database error)", accountId);
        return false;
    }
    if (head->Fetch()[0].GetUInt64() == 0)
    {
        if (head->Fetch()[1].GetUInt64() != 0)
            sLog.outError("Hearthstone: account %u has ladder offers but no offer state row; they are replaced by the next draw",
                accountId);
        return true;
    }

    std::unique_ptr<QueryResult> stateRows(CharacterDatabase.PQuery(
        "SELECT %s FROM `hearthstone_ladder_offer_state` WHERE `account_id` = %u", Hs::Ladder::kOfferStateColumns, accountId));
    Hs::Ladder::OfferStateRow state;
    if (!stateRows || !Hs::Ladder::OfferStateRowFromCells(CellsOf(stateRows->Fetch(), Hs::Ladder::kOfferStateColumnCount), state))
    {
        sLog.outError("Hearthstone: the ladder offer state row of account %u is malformed", accountId);
        return false;
    }

    std::vector<Hs::Ladder::OfferRow> slots;
    if (head->Fetch()[1].GetUInt64() != 0)
    {
        std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery(
            "SELECT %s FROM `hearthstone_ladder_offer` WHERE `account_id` = %u ORDER BY `slot`", Hs::Ladder::kOfferColumns,
            accountId));
        if (!rows)
        {
            sLog.outError("Hearthstone: cannot read the ladder offers of account %u", accountId);
            return false;
        }
        do
        {
            Hs::Ladder::OfferRow row;
            if (!Hs::Ladder::OfferRowFromCells(CellsOf(rows->Fetch(), Hs::Ladder::kOfferColumnCount), row))
            {
                sLog.outError("Hearthstone: a ladder offer row of account %u is malformed", accountId);
                return false;
            }
            slots.push_back(row);
        } while (rows->NextRow());
    }

    std::string error;
    Hs::Ladder::OfferSet set;
    if (state.account != accountId || !Hs::Ladder::BuildOfferSet(state, slots, set, error))
    {
        sLog.outError("Hearthstone: the stored ladder offers of account %u are not valid: %s", accountId,
            error.empty() ? "wrong account" : error.c_str());
        return false;
    }
    out = std::move(set);
    found = true;
    return true;
}

bool HsLadderDbStore::SaveOffers(Hs::Ladder::OfferSet const& set)
{
    return Commit(Hs::Ladder::OfferSaveSql(set), "offers");
}

bool HsLadderDbStore::ResetOffers()
{
    return Commit(Hs::Ladder::OfferResetSql(), "offer reset");
}

bool HsLadderDbStore::LoadMilestones(uint32_t accountId, std::vector<std::string>& out)
{
    out.clear();
    std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery("SELECT (SELECT COUNT(*) FROM `%s` WHERE `account_id` = %u)",
        Hs::Ladder::kTableMilestone, accountId));
    if (!head)
    {
        sLog.outError("Hearthstone: cannot read the ladder milestones of account %u "
            "(character migration 0018_hearthstone_ladder_milestone.sql not applied, or database error)", accountId);
        return false;
    }
    if (head->Fetch()[0].GetUInt64() == 0)
        return true;
    std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery("SELECT `milestone` FROM `%s` WHERE `account_id` = %u ORDER BY `reached_at`",
        Hs::Ladder::kTableMilestone, accountId));
    if (!rows)
        return false;
    do
        out.push_back(rows->Fetch()[0].GetCppString());
    while (rows->NextRow());
    return true;
}

bool HsLadderDbStore::SaveMilestone(uint32_t accountId, std::string const& milestone)
{
    return Commit({ Hs::Ladder::MilestoneInsertSql(accountId, milestone) }, "milestone");
}

bool HsLadderDbStore::Reset()
{
    return Commit(Hs::Ladder::ResetSql(), "reset");
}

bool HsLadderDbStore::LoadGames(Hs::Ladder::EntrantId npc, uint32_t limit, std::vector<Hs::Ladder::GameRow>& out)
{
    out.clear();
    unsigned long long const id = static_cast<unsigned long long>(npc);
    std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
        "SELECT (SELECT COUNT(*) FROM `hearthstone_ladder_game` WHERE `npc_a` = %llu OR `npc_b` = %llu)", id, id));
    if (!head)
    {
        sLog.outError("Hearthstone: cannot read the ladder game log of entrant %llu", id);
        return false;
    }
    if (head->Fetch()[0].GetUInt64() == 0)
        return true;
    std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery(
        "(SELECT %s FROM `hearthstone_ladder_game` WHERE `npc_a` = %llu ORDER BY `round_no` DESC LIMIT %u) UNION ALL "
        "(SELECT %s FROM `hearthstone_ladder_game` WHERE `npc_b` = %llu ORDER BY `round_no` DESC LIMIT %u) "
        "ORDER BY `round_no` DESC LIMIT %u", Hs::Ladder::kGameColumns, id, limit, Hs::Ladder::kGameColumns, id, limit,
        limit));
    if (!rows)
    {
        sLog.outError("Hearthstone: cannot read the ladder game log rows of entrant %llu", id);
        return false;
    }
    do
    {
        Hs::Ladder::GameRow row;
        if (!Hs::Ladder::GameRowFromCells(CellsOf(rows->Fetch(), Hs::Ladder::kGameColumnCount), row))
        {
            sLog.outError("Hearthstone: a ladder game row is malformed");
            out.clear();
            return false;
        }
        out.push_back(std::move(row));
    } while (rows->NextRow());
    return true;
}

HsLadderDbStore& HsLadderStore()
{
    static HsLadderDbStore store;
    return store;
}

bool CheckHsLadderTables()
{
    char const* const tables[] = {
        Hs::Ladder::kTableLadder,
        Hs::Ladder::kTableEntrant,
        Hs::Ladder::kTableVs,
        Hs::Ladder::kTableGame,
    };
    bool all = true;
    for (char const* table : tables)
    {
        std::unique_ptr<QueryResult> found(CharacterDatabase.PQuery("SHOW TABLES LIKE '%s'", table));
        if (!found)
        {
            all = false;
            sLog.outError("Hearthstone: table %s is missing in the character database "
                "(migration 0015_hearthstone_ladder.sql not applied? check Database.AutoUpdate.*): the ladder is "
                "unavailable", table);
        }
    }
    return all;
}

bool CheckHsLadderOfferTables()
{
    char const* const tables[] = {
        Hs::Ladder::kTableOffer,
        Hs::Ladder::kTableOfferState,
    };
    bool all = true;
    for (char const* table : tables)
    {
        std::unique_ptr<QueryResult> found(CharacterDatabase.PQuery("SHOW TABLES LIKE '%s'", table));
        if (!found)
        {
            all = false;
            sLog.outError("Hearthstone: table %s is missing in the character database "
                "(migration 0016_hearthstone_ladder_offer.sql not applied? check Database.AutoUpdate.*): the ladder "
                "chairs are unavailable", table);
        }
    }
    return all;
}
