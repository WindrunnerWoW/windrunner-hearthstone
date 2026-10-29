#include "HsLadderWorld.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <map>

namespace Hs
{
namespace Ladder
{
    namespace
    {
        bool ParseU32(std::string const& text, uint32_t& out)
        {
            if (text.empty() || text.size() > 10)
                return false;
            uint64_t value = 0;
            for (char c : text)
            {
                if (c < '0' || c > '9')
                    return false;
                value = value * 10 + static_cast<uint64_t>(c - '0');
            }
            if (value > UINT32_MAX)
                return false;
            out = static_cast<uint32_t>(value);
            return true;
        }

        // A decimal number as the database prints a FLOAT or DOUBLE ("9821.464844", "-1224.17", "1e-05"). Not
        // checked for finite here: BuildWorld reports a coordinate that is not.
        bool ParseDouble(std::string const& text, double& out)
        {
            if (text.empty() || text.size() > 40)
                return false;
            char* end = nullptr;
            errno = 0;
            double const v = std::strtod(text.c_str(), &end);
            if (end != text.c_str() + text.size() || errno == ERANGE)
                return false;
            out = v;
            return true;
        }

        bool Finite(double v) { return std::isfinite(v) != 0; }

        std::string U(uint64_t v) { return std::to_string(v); }

        WorldPoint Point(double x, double y, double z)
        {
            WorldPoint p;
            p.x = static_cast<float>(x);
            p.y = static_cast<float>(y);
            p.z = static_cast<float>(z);
            return p;
        }
    }

    char const* LineKindCode(LineKind kind)
    {
        switch (kind)
        {
            case LineKind::Win: return "win";
            case LineKind::Loss: return "loss";
            case LineKind::Draw: return "draw";
        }
        return "win";
    }

    bool ParseLineKind(std::string const& code, LineKind& out)
    {
        if (code == "win")
            out = LineKind::Win;
        else if (code == "loss")
            out = LineKind::Loss;
        else if (code == "draw")
            out = LineKind::Draw;
        else
            return false;
        return true;
    }

    LineKind LineKindForPlayerResult(Result playerResult)
    {
        switch (playerResult)
        {
            case Result::Win: return LineKind::Loss; // the player won: the NPC lost
            case Result::Loss: return LineKind::Win;
            case Result::Draw: break;
        }
        return LineKind::Draw;
    }

    // ---- cells -> rows ----------------------------------------------------------------------------------

    bool WorldTableRowFromCells(std::vector<std::string> const& c, WorldTableRow& out)
    {
        WorldTableRow r;
        if (c.size() != kWorldTableColumnCount || !ParseU32(c[0], r.tableGuid) || !ParseU32(c[2], r.mapId) ||
            !ParseU32(c[3], r.chairGuid) || !ParseU32(c[4], r.opponentChairGuid) || !ParseDouble(c[5], r.spawnX) ||
            !ParseDouble(c[6], r.spawnY) || !ParseDouble(c[7], r.spawnZ) || !ParseDouble(c[8], r.spawnO) ||
            !ParseDouble(c[9], r.stopX) || !ParseDouble(c[10], r.stopY) || !ParseDouble(c[11], r.stopZ))
            return false;
        r.city = c[1];
        out = r;
        return true;
    }

    bool WorldPathRowFromCells(std::vector<std::string> const& c, WorldPathRow& out)
    {
        WorldPathRow r;
        if (c.size() != kWorldPathColumnCount || !ParseU32(c[0], r.tableGuid) || !ParseU32(c[1], r.pointNo) ||
            !ParseDouble(c[2], r.x) || !ParseDouble(c[3], r.y) || !ParseDouble(c[4], r.z))
            return false;
        out = r;
        return true;
    }

    bool WorldLineRowFromCells(std::vector<std::string> const& c, WorldLineRow& out)
    {
        WorldLineRow r;
        if (c.size() != kWorldLineColumnCount || !ParseU32(c[0], r.id) || !ParseLineKind(c[1], r.kind))
            return false;
        r.text = c[2];
        out = r;
        return true;
    }

    bool WorldBlockRowFromCells(std::vector<std::string> const& c, WorldBlockRow& out)
    {
        WorldBlockRow r;
        if (c.size() != kWorldBlockColumnCount || !ParseU32(c[0], r.creatureEntry))
            return false;
        r.reason = c[1];
        out = r;
        return true;
    }

    // ---- the data ---------------------------------------------------------------------------------------

    std::vector<WorldPoint> LadderTable::Route() const
    {
        std::vector<WorldPoint> route;
        route.push_back(spawn);
        route.insert(route.end(), path.begin(), path.end());
        route.push_back(stop);
        return route;
    }

    float LadderTable::RouteLength() const
    {
        std::vector<WorldPoint> const route = Route();
        double length = 0;
        for (std::size_t i = 1; i < route.size(); ++i)
        {
            double const dx = static_cast<double>(route[i].x) - route[i - 1].x;
            double const dy = static_cast<double>(route[i].y) - route[i - 1].y;
            double const dz = static_cast<double>(route[i].z) - route[i - 1].z;
            length += std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        return static_cast<float>(length);
    }

    LadderTable const* LadderWorld::FindByChair(uint32_t chairGuid) const
    {
        for (LadderTable const& t : tables)
            if (t.chairGuid == chairGuid)
                return &t;
        return nullptr;
    }

    LadderTable const* LadderWorld::FindByTable(uint32_t tableGuid) const
    {
        for (LadderTable const& t : tables)
            if (t.tableGuid == tableGuid)
                return &t;
        return nullptr;
    }

    std::size_t LadderWorld::LineCount(LineKind kind) const
    {
        std::size_t n = 0;
        for (LadderLine const& l : lines)
            n += l.kind == kind ? 1 : 0;
        return n;
    }

    LadderLine const* LadderWorld::PickLine(LineKind kind, uint32_t seed) const
    {
        std::size_t const n = LineCount(kind);
        if (n == 0)
            return nullptr;
        std::size_t index = Mix(seed, 0x11AE0000u + static_cast<uint32_t>(kind)) % n;
        for (LadderLine const& l : lines)
            if (l.kind == kind && index-- == 0)
                return &l;
        return nullptr;
    }

    void BuildWorld(std::vector<WorldTableRow> const& tableRows, std::vector<WorldPathRow> const& pathRows,
                    std::vector<WorldLineRow> const& lineRows, std::vector<WorldBlockRow> const& blockRows,
                    LadderWorld& out, std::vector<std::string>& problems)
    {
        LadderWorld w;

        // Tables, in table_guid order.
        std::vector<WorldTableRow> sorted = tableRows;
        std::stable_sort(sorted.begin(), sorted.end(),
            [](WorldTableRow const& a, WorldTableRow const& b) { return a.tableGuid < b.tableGuid; });
        std::set<uint32_t> usedGuids; // every gameobject guid of an accepted table (table, chairs)
        std::map<uint32_t, std::size_t> indexOf;
        for (WorldTableRow const& r : sorted)
        {
            std::string const name = "table " + U(r.tableGuid) + (r.city.empty() ? "" : " (" + r.city + ")");
            if (r.tableGuid == 0 || r.chairGuid == 0 || r.opponentChairGuid == 0)
            {
                problems.push_back(name + ": a guid is 0, the table is left out");
                continue;
            }
            if (r.chairGuid == r.opponentChairGuid || r.chairGuid == r.tableGuid || r.opponentChairGuid == r.tableGuid)
            {
                problems.push_back(name + ": the table and the two chairs must be three different gameobjects, the table is left out");
                continue;
            }
            if (usedGuids.count(r.tableGuid) || usedGuids.count(r.chairGuid) || usedGuids.count(r.opponentChairGuid))
            {
                problems.push_back(name + ": a guid is used by another table, the table is left out");
                continue;
            }
            if (r.city.empty())
            {
                problems.push_back(name + ": the city is empty, the table is left out");
                continue;
            }
            if (!Finite(r.spawnX) || !Finite(r.spawnY) || !Finite(r.spawnZ) || !Finite(r.spawnO) || !Finite(r.stopX) ||
                !Finite(r.stopY) || !Finite(r.stopZ))
            {
                problems.push_back(name + ": a coordinate is not a finite number, the table is left out");
                continue;
            }
            if (r.city.size() > kCityBytes)
                problems.push_back(name + ": the city name is longer than " + U(kCityBytes) + " bytes");
            LadderTable t;
            t.tableGuid = r.tableGuid;
            t.city = r.city;
            t.mapId = r.mapId;
            t.chairGuid = r.chairGuid;
            t.opponentChairGuid = r.opponentChairGuid;
            t.spawn = Point(r.spawnX, r.spawnY, r.spawnZ);
            t.spawnO = static_cast<float>(r.spawnO);
            t.stop = Point(r.stopX, r.stopY, r.stopZ);
            usedGuids.insert(r.tableGuid);
            usedGuids.insert(r.chairGuid);
            usedGuids.insert(r.opponentChairGuid);
            indexOf[t.tableGuid] = w.tables.size();
            w.tables.push_back(t);
        }

        // Path points: by table, in point_no order. A repeated number leaves the table out (the route is unclear).
        std::vector<WorldPathRow> points = pathRows;
        std::stable_sort(points.begin(), points.end(), [](WorldPathRow const& a, WorldPathRow const& b) {
            return a.tableGuid != b.tableGuid ? a.tableGuid < b.tableGuid : a.pointNo < b.pointNo;
        });
        std::set<uint32_t> broken;
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            WorldPathRow const& p = points[i];
            auto const at = indexOf.find(p.tableGuid);
            if (at == indexOf.end())
            {
                if (!broken.count(p.tableGuid) && !std::any_of(sorted.begin(), sorted.end(),
                        [&](WorldTableRow const& r) { return r.tableGuid == p.tableGuid; }))
                    problems.push_back("path point " + U(p.pointNo) + " of table " + U(p.tableGuid) + ": no such table, ignored");
                continue;
            }
            if (i > 0 && points[i - 1].tableGuid == p.tableGuid && points[i - 1].pointNo == p.pointNo)
            {
                if (broken.insert(p.tableGuid).second)
                    problems.push_back("table " + U(p.tableGuid) + ": path point " + U(p.pointNo) +
                        " is used twice, the table is left out");
                continue;
            }
            if (!Finite(p.x) || !Finite(p.y) || !Finite(p.z))
            {
                if (broken.insert(p.tableGuid).second)
                    problems.push_back("table " + U(p.tableGuid) + ": path point " + U(p.pointNo) +
                        " has a coordinate that is not a finite number, the table is left out");
                continue;
            }
            w.tables[at->second].path.push_back(Point(p.x, p.y, p.z));
        }
        if (!broken.empty())
        {
            std::vector<LadderTable> kept;
            for (LadderTable const& t : w.tables)
                if (!broken.count(t.tableGuid))
                    kept.push_back(t);
            w.tables = kept;
        }

        // Lines, in id order.
        std::vector<WorldLineRow> lines = lineRows;
        std::stable_sort(lines.begin(), lines.end(), [](WorldLineRow const& a, WorldLineRow const& b) { return a.id < b.id; });
        std::set<uint32_t> lineIds;
        for (WorldLineRow const& r : lines)
        {
            if (!lineIds.insert(r.id).second)
            {
                problems.push_back("line " + U(r.id) + " is stored twice, the later one is ignored");
                continue;
            }
            if (r.text.empty())
            {
                problems.push_back("line " + U(r.id) + " has no text, ignored");
                continue;
            }
            if (r.text.size() > kLineBytes)
                problems.push_back("line " + U(r.id) + " is longer than " + U(kLineBytes) + " bytes");
            LadderLine l;
            l.id = r.id;
            l.kind = r.kind;
            l.text = r.text;
            w.lines.push_back(l);
        }
        for (LineKind kind : {LineKind::Win, LineKind::Loss, LineKind::Draw})
            if (w.LineCount(kind) == 0)
                problems.push_back(std::string("no ") + LineKindCode(kind) + " line: the NPCs say nothing after such a game");

        // Blocked entries, in creature_entry order.
        std::vector<WorldBlockRow> blocks = blockRows;
        std::stable_sort(blocks.begin(), blocks.end(),
            [](WorldBlockRow const& a, WorldBlockRow const& b) { return a.creatureEntry < b.creatureEntry; });
        for (WorldBlockRow const& r : blocks)
        {
            if (r.creatureEntry == 0)
            {
                problems.push_back("a block row has creature entry 0, ignored");
                continue;
            }
            if (!w.blocked.insert(NpcId(r.creatureEntry)).second)
            {
                problems.push_back("creature entry " + U(r.creatureEntry) + " is blocked twice");
                continue;
            }
            w.blockRows.push_back(r);
        }

        out = w;
    }

    std::string RosterRiskSummary(std::vector<RosterRisk> const& risks, std::set<EntrantId> const& blocked, std::size_t listMax)
    {
        std::size_t scripts = 0, scriptsBlocked = 0, ais = 0, aisBlocked = 0;
        std::vector<RosterRisk const*> open; // a script of its own and not blocked
        for (RosterRisk const& r : risks)
        {
            bool const isBlocked = blocked.count(NpcId(r.entry)) != 0;
            if (!r.scriptName.empty())
            {
                ++scripts;
                scriptsBlocked += isBlocked ? 1 : 0;
                if (!isBlocked)
                    open.push_back(&r);
            }
            if (!r.aiName.empty())
            {
                ++ais;
                aisBlocked += isBlocked ? 1 : 0;
            }
        }
        if (scripts == 0 && ais == 0)
            return std::string();
        std::string text = std::to_string(scripts) + " roster NPC(s) with a script of their own (" +
            std::to_string(scriptsBlocked) + " blocked), " + std::to_string(ais) + " with an AI name (" +
            std::to_string(aisBlocked) + " blocked)";
        if (!open.empty())
        {
            text += "; scripted and not blocked:";
            for (std::size_t i = 0; i < open.size() && i < listMax; ++i)
                text += (i ? ", " : " ") + open[i]->name + " (" + std::to_string(open[i]->entry) + ", " + open[i]->scriptName + ")";
            if (open.size() > listMax)
                text += ", ...";
        }
        return text;
    }
}
}
