/*
 * World NPC Ladder: the owner data of the ladder tables, read from the WORLD database. See
 * data/sql/world/0049_hearthstone_ladder_tables.sql.
 *
 * PURE C++ (standard library and the ladder rules, no core headers, no database). src/HsLadderWorldStore.cpp reads
 * the four tables and feeds the cells it reads into the parsers here; BuildWorld turns the rows into plain structs
 * and reports what it had to leave out, so the one place that knows the column order is this file and
 * tests/ladder_tests.cpp can check every column against the migration.
 *
 * Tables:
 *   hearthstone_ladder_table   one row per table: the table gameobject, the player's chair, the opponent's chair,
 *                              the opponent's spawn point (with facing) and the stop point beside its chair
 *   hearthstone_ladder_path    the points between spawn and stop, in point_no order
 *   hearthstone_ladder_line    what an NPC says after the match (win / loss / draw, from the NPC's point of view)
 *   hearthstone_ladder_block   creature entries that are kept out of the offers
 */

#ifndef MOD_HEARTHSTONE_HS_LADDER_WORLD_H
#define MOD_HEARTHSTONE_HS_LADDER_WORLD_H

#include "HsLadderRules.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace Hs
{
namespace Ladder
{
    // ---- tables and columns (the order of the cells) -----------------------------------------------------

    constexpr char const* kWorldTable = "hearthstone_ladder_table";
    constexpr char const* kWorldPath = "hearthstone_ladder_path";
    constexpr char const* kWorldLine = "hearthstone_ladder_line";
    constexpr char const* kWorldBlock = "hearthstone_ladder_block";

    constexpr char const* kWorldTableColumns =
        "`table_guid`, `city`, `map_id`, `chair_guid`, `opponent_chair_guid`, `spawn_x`, `spawn_y`, `spawn_z`, "
        "`spawn_o`, `stop_x`, `stop_y`, `stop_z`";
    constexpr std::size_t kWorldTableColumnCount = 12;
    constexpr char const* kWorldPathColumns = "`table_guid`, `point_no`, `x`, `y`, `z`";
    constexpr std::size_t kWorldPathColumnCount = 5;
    constexpr char const* kWorldLineColumns = "`id`, `kind`, `text`";
    constexpr std::size_t kWorldLineColumnCount = 3;
    constexpr char const* kWorldBlockColumns = "`creature_entry`, `reason`";
    constexpr std::size_t kWorldBlockColumnCount = 2;

    // Text limits of the VARCHAR columns (the loader cuts nothing, the checks only complain).
    constexpr std::size_t kCityBytes = 32;
    constexpr std::size_t kLineBytes = 255;
    constexpr std::size_t kReasonBytes = 120;

    // The gameobject template of the ladder chair (a copy of Wooden Chair 101766 with the ladder script).
    constexpr uint32_t kLadderChairEntry = 919100;
    constexpr char const* kLadderChairScript = "hearthstone_ladder_chair";
    // The creature template that carries the chair glow spell (a copy of Invisible Stalker 15214 that players can see,
    // world migration 0057; its model is invisible, only the spell shows).
    constexpr uint32_t kLadderChairGlowEntry = 919101;

    // ---- rows ---------------------------------------------------------------------------------------------

    struct WorldTableRow
    {
        uint32_t tableGuid = 0;
        std::string city;
        uint32_t mapId = 0;
        uint32_t chairGuid = 0, opponentChairGuid = 0;
        double spawnX = 0, spawnY = 0, spawnZ = 0, spawnO = 0;
        double stopX = 0, stopY = 0, stopZ = 0;
    };

    struct WorldPathRow
    {
        uint32_t tableGuid = 0;
        uint32_t pointNo = 0;
        double x = 0, y = 0, z = 0;
    };

    enum class LineKind : uint8_t { Win, Loss, Draw }; // from the NPC's point of view
    char const* LineKindCode(LineKind kind);           // "win" "loss" "draw"
    bool ParseLineKind(std::string const& code, LineKind& out);
    // The kind of line an NPC says after a game, from the player's result (the NPC wins when the player loses).
    LineKind LineKindForPlayerResult(Result playerResult);

    struct WorldLineRow
    {
        uint32_t id = 0;
        LineKind kind = LineKind::Win;
        std::string text;
    };

    struct WorldBlockRow
    {
        uint32_t creatureEntry = 0;
        std::string reason;
    };

    // ---- cells -> rows (false: a wrong cell count, a cell that is not a number, an unknown line kind) -----

    bool WorldTableRowFromCells(std::vector<std::string> const& cells, WorldTableRow& out);
    bool WorldPathRowFromCells(std::vector<std::string> const& cells, WorldPathRow& out);
    bool WorldLineRowFromCells(std::vector<std::string> const& cells, WorldLineRow& out);
    bool WorldBlockRowFromCells(std::vector<std::string> const& cells, WorldBlockRow& out);

    // ---- the data ---------------------------------------------------------------------------------------

    struct WorldPoint
    {
        float x = 0, y = 0, z = 0;
    };

    // One ladder table of a host city. The player sits on chairGuid (the ladder chair), the NPC appears at spawn,
    // walks through path (in order, may be empty) to stop and sits on opponentChairGuid.
    struct LadderTable
    {
        uint32_t tableGuid = 0;
        std::string city;
        uint32_t mapId = 0;
        uint32_t chairGuid = 0, opponentChairGuid = 0;
        WorldPoint spawn;
        float spawnO = 0;
        std::vector<WorldPoint> path;
        WorldPoint stop;

        // Spawn, path points and stop in walking order.
        std::vector<WorldPoint> Route() const;
        // The length of Route() in yards.
        float RouteLength() const;
    };

    struct LadderLine
    {
        uint32_t id = 0;
        LineKind kind = LineKind::Win;
        std::string text;
    };

    struct LadderWorld
    {
        std::vector<LadderTable> tables;      // table_guid order
        std::vector<LadderLine> lines;        // id order
        std::set<EntrantId> blocked;          // NpcId(creature entry)
        std::vector<WorldBlockRow> blockRows; // creature_entry order, with the reasons

        // The table whose player chair is chairGuid, or the table with that table guid (nullptr: none).
        LadderTable const* FindByChair(uint32_t chairGuid) const;
        LadderTable const* FindByTable(uint32_t tableGuid) const;
        // The line for a kind picked from the seed (nullptr: no line of that kind). The same seed gives the same line.
        LadderLine const* PickLine(LineKind kind, uint32_t seed) const;
        std::size_t LineCount(LineKind kind) const;
    };

    // Turns the rows into the data. A table that cannot work is left out and every such thing is described in
    // `problems` (one text each, for the log): a repeated or zero guid, the same chair twice, an empty city, a
    // coordinate that is not a finite number, a path point number used twice, a path point of an unknown table, a
    // line with no text or a repeated id, a repeated block entry. Lines with text longer than the column are
    // reported too and kept. A kind with no line at all is reported (nothing is said then). Rows may come in any
    // order.
    void BuildWorld(std::vector<WorldTableRow> const& tables, std::vector<WorldPathRow> const& paths,
                    std::vector<WorldLineRow> const& lines, std::vector<WorldBlockRow> const& blocks,
                    LadderWorld& out, std::vector<std::string>& problems);

    // ---- roster risks -------------------------------------------------------------------------------------

    // A roster NPC whose creature_template has a script or an AI of its own (read by the loader, reported at start).
    struct RosterRisk
    {
        uint32_t entry = 0;
        std::string name;
        std::string scriptName; // creature_template.script_name
        std::string aiName;     // creature_template.ai_name
    };

    // One log line for the roster NPCs with a script or AI of their own: the counts, and the NPCs with a script that
    // are not blocked (at most `listMax` of them, by name). Empty when there are none.
    std::string RosterRiskSummary(std::vector<RosterRisk> const& risks, std::set<EntrantId> const& blocked,
                                  std::size_t listMax = 12);
}
}

#endif
