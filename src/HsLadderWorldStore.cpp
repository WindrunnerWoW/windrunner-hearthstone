#include "HsLadderWorldStore.h"

#include "Database/DatabaseEnv.h"
#include "Log.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    // The text of the first n fields of the current row (NULL is the empty text).
    std::vector<std::string> CellsOf(Field* f, std::size_t n)
    {
        std::vector<std::string> cells;
        for (std::size_t i = 0; i < n; ++i)
            cells.push_back(f[i].IsNULL() ? std::string() : f[i].GetCppString());
        return cells;
    }

    bool TableExists(char const* table)
    {
        std::unique_ptr<QueryResult> found(WorldDatabase.PQuery("SHOW TABLES LIKE '%s'", table));
        return found != nullptr;
    }

    // Reads every row of `table` (ordered by `order`) as cells and parses it. Rows that do not parse are counted in
    // `bad`.
    template <typename Row, typename Parse>
    void ReadRows(char const* table, char const* columns, std::size_t columnCount, char const* order, Parse parse,
                  std::vector<Row>& out, std::size_t& bad)
    {
        std::unique_ptr<QueryResult> rows(WorldDatabase.PQuery("SELECT %s FROM `%s` ORDER BY %s", columns, table, order));
        if (!rows)
        {
            // No result: an empty table (the SHOW TABLES check before ruled out a missing one), or a query that failed,
            // for example on a column that does not match the migration. The second must not pass silently.
            std::unique_ptr<QueryResult> count(WorldDatabase.PQuery("SELECT COUNT(*) FROM `%s`", table));
            if (count && count->Fetch()[0].GetUInt64() > 0)
                sLog.outError("Hearthstone: table %s has rows but they could not be read (columns %s; migration "
                    "0049_hearthstone_ladder_tables.sql up to date?)", table, columns);
            return;
        }
        do
        {
            Row row;
            if (parse(CellsOf(rows->Fetch(), columnCount), row))
                out.push_back(std::move(row));
            else
                ++bad;
        } while (rows->NextRow());
    }
}

bool CheckHsLadderWorldTables()
{
    char const* const tables[] = {
        Hs::Ladder::kWorldTable,
        Hs::Ladder::kWorldPath,
        Hs::Ladder::kWorldLine,
        Hs::Ladder::kWorldBlock,
    };
    bool all = true;
    for (char const* table : tables)
        if (!TableExists(table))
        {
            all = false;
            sLog.outError("Hearthstone: table %s is missing in the world database "
                "(migration 0049_hearthstone_ladder_tables.sql not applied? check Database.AutoUpdate.*): the ladder "
                "chairs are unavailable", table);
        }
    return all;
}

bool LoadHsLadderWorld(Hs::Ladder::LadderWorld& out)
{
    namespace HL = Hs::Ladder;
    out = HL::LadderWorld();
    if (!CheckHsLadderWorldTables())
        return false;

    std::vector<HL::WorldTableRow> tables;
    std::vector<HL::WorldPathRow> paths;
    std::vector<HL::WorldLineRow> lines;
    std::vector<HL::WorldBlockRow> blocks;
    std::size_t bad = 0;
    ReadRows(HL::kWorldTable, HL::kWorldTableColumns, HL::kWorldTableColumnCount, "`table_guid`",
             HL::WorldTableRowFromCells, tables, bad);
    ReadRows(HL::kWorldPath, HL::kWorldPathColumns, HL::kWorldPathColumnCount, "`table_guid`, `point_no`",
             HL::WorldPathRowFromCells, paths, bad);
    ReadRows(HL::kWorldLine, HL::kWorldLineColumns, HL::kWorldLineColumnCount, "`id`", HL::WorldLineRowFromCells, lines, bad);
    ReadRows(HL::kWorldBlock, HL::kWorldBlockColumns, HL::kWorldBlockColumnCount, "`creature_entry`",
             HL::WorldBlockRowFromCells, blocks, bad);
    if (bad != 0)
    {
        sLog.outError("Hearthstone: %u row(s) of the ladder world tables are malformed (a number that is not one, or "
            "an unknown line kind): the ladder chairs are unavailable", static_cast<uint32_t>(bad));
        return false;
    }

    HL::LadderWorld world;
    std::vector<std::string> problems;
    HL::BuildWorld(tables, paths, lines, blocks, world, problems);
    for (std::string const& problem : problems)
        sLog.outError("Hearthstone: ladder world data: %s", problem.c_str());

    // Every table needs its three gameobjects: the table, the ladder chair and the opponent's chair.
    std::vector<HL::LadderTable> usable;
    for (HL::LadderTable const& t : world.tables)
    {
        std::map<uint32_t, std::pair<uint32_t, uint32_t>> spawns; // guid -> (template entry, map)
        std::unique_ptr<QueryResult> rows(WorldDatabase.PQuery(
            "SELECT `guid`, `id`, `map` FROM `gameobject` WHERE `guid` IN (%u, %u, %u)", t.tableGuid, t.chairGuid,
            t.opponentChairGuid));
        if (rows)
            do
            {
                Field* f = rows->Fetch();
                spawns[f[0].GetUInt32()] = std::make_pair(f[1].GetUInt32(), f[2].GetUInt32());
            } while (rows->NextRow());
        bool ok = true;
        auto check = [&](uint32_t guid, char const* what, uint32_t wantEntry) {
            auto const at = spawns.find(guid);
            if (at == spawns.end())
            {
                sLog.outError("Hearthstone: ladder table %u (%s): the %s (gameobject guid %u) is not spawned", t.tableGuid,
                    t.city.c_str(), what, guid);
                ok = false;
            }
            else if (wantEntry != 0 && at->second.first != wantEntry)
            {
                sLog.outError("Hearthstone: ladder table %u (%s): the %s (gameobject guid %u) is entry %u, not %u "
                    "(migration 0049_hearthstone_ladder_tables.sql applied?)", t.tableGuid, t.city.c_str(), what, guid,
                    at->second.first, wantEntry);
                ok = false;
            }
            else if (at->second.second != t.mapId)
            {
                sLog.outError("Hearthstone: ladder table %u (%s): the %s (gameobject guid %u) is on map %u, not %u",
                    t.tableGuid, t.city.c_str(), what, guid, at->second.second, t.mapId);
                ok = false;
            }
        };
        check(t.tableGuid, "table", 0);
        check(t.chairGuid, "ladder chair", HL::kLadderChairEntry);
        check(t.opponentChairGuid, "opponent chair", 0);
        if (ok)
            usable.push_back(t);
    }
    world.tables = std::move(usable);

    if (world.tables.empty())
        sLog.outError("Hearthstone: no ladder table works (see above): the ladder chairs are unavailable");
    sLog.outString("Hearthstone: ladder world data: %u table(s), %u line(s) (%u win, %u loss, %u draw), %u blocked NPC(s)",
        static_cast<uint32_t>(world.tables.size()), static_cast<uint32_t>(world.lines.size()),
        static_cast<uint32_t>(world.LineCount(HL::LineKind::Win)), static_cast<uint32_t>(world.LineCount(HL::LineKind::Loss)),
        static_cast<uint32_t>(world.LineCount(HL::LineKind::Draw)), static_cast<uint32_t>(world.blocked.size()));
    out = std::move(world);
    return true;
}

bool LoadHsLadderRosterRisks(std::vector<Hs::Ladder::RosterRisk>& out)
{
    out.clear();
    if (!TableExists("hearthstone_finder_npc"))
    {
        sLog.outError("Hearthstone: table hearthstone_finder_npc is missing in the world database "
            "(migration 0003_hearthstone_finder.sql not applied?)");
        return false;
    }
    std::unique_ptr<QueryResult> rows(WorldDatabase.PQuery(
        "SELECT n.`creature_entry`, t.`name`, t.`script_name`, t.`ai_name` FROM `hearthstone_finder_npc` n "
        "JOIN `creature_template` t ON t.`entry` = n.`creature_entry` WHERE n.`enabled` <> 0 AND "
        "(t.`script_name` <> '' OR t.`ai_name` <> '') ORDER BY n.`creature_entry`"));
    if (!rows)
        return true;
    do
    {
        Field* f = rows->Fetch();
        Hs::Ladder::RosterRisk risk;
        risk.entry = f[0].GetUInt32();
        risk.name = f[1].IsNULL() ? std::string() : f[1].GetCppString();
        risk.scriptName = f[2].IsNULL() ? std::string() : f[2].GetCppString();
        risk.aiName = f[3].IsNULL() ? std::string() : f[3].GetCppString();
        out.push_back(risk);
    } while (rows->NextRow());
    return true;
}
