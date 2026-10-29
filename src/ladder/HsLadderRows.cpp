#include "HsLadderRows.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <set>

namespace Hs
{
namespace Ladder
{
    namespace
    {
        constexpr std::size_t kChunkRows = 100; // rows per INSERT / REPLACE statement

        // Cuts text to at most maxBytes without splitting a UTF-8 character.
        std::string Cut(std::string s, std::size_t maxBytes)
        {
            if (s.size() <= maxBytes)
                return s;
            std::size_t n = maxBytes;
            while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0u) == 0x80u)
                --n;
            s.resize(n);
            return s;
        }

        bool ParseU64(std::string const& text, uint64_t& out)
        {
            if (text.empty() || text.size() > 20)
                return false;
            uint64_t value = 0;
            for (char c : text)
            {
                if (c < '0' || c > '9')
                    return false;
                uint64_t const digit = static_cast<uint64_t>(c - '0');
                if (value > (UINT64_MAX - digit) / 10)
                    return false;
                value = value * 10 + digit;
            }
            out = value;
            return true;
        }

        bool ParseU32(std::string const& text, uint32_t& out)
        {
            uint64_t v = 0;
            if (!ParseU64(text, v) || v > UINT32_MAX)
                return false;
            out = static_cast<uint32_t>(v);
            return true;
        }

        bool ParseI32(std::string const& text, int32_t& out)
        {
            bool const negative = !text.empty() && text[0] == '-';
            uint64_t v = 0;
            if (!ParseU64(negative ? text.substr(1) : text, v) || v > (negative ? 2147483648ull : 2147483647ull))
                return false;
            out = negative ? static_cast<int32_t>(-static_cast<int64_t>(v)) : static_cast<int32_t>(v);
            return true;
        }

        bool ParseProfile(std::string const& code, Match::AiProfile& out)
        {
            if (code == "normal")
                out = Match::AiProfile::Normal;
            else if (code == "hard")
                out = Match::AiProfile::Hard;
            else
                return false;
            return true;
        }

        std::string Quote(SqlEscape const& escape, std::string const& text)
        {
            return "'" + escape(text) + "'";
        }

        std::string U(uint64_t v) { return std::to_string(v); }
        std::string S(int64_t v) { return std::to_string(v); }

        // "REPLACE INTO `table` (columns) VALUES (...), (...)" in chunks of kChunkRows tuples.
        std::vector<std::string> ReplaceChunks(char const* table, char const* columns, std::vector<std::string> const& tuples)
        {
            std::vector<std::string> out;
            for (std::size_t i = 0; i < tuples.size(); i += kChunkRows)
            {
                std::string sql = std::string("REPLACE INTO `") + table + "` (" + columns + ") VALUES ";
                for (std::size_t j = i; j < tuples.size() && j < i + kChunkRows; ++j)
                    sql += (j > i ? ", " : "") + tuples[j];
                out.push_back(sql);
            }
            return out;
        }
    }

    // ---- state <-> rows ----------------------------------------------------------------------------------

    LadderRow ToLadderRow(LadderState const& state, uint32_t rulesVersion)
    {
        LadderRow row;
        row.seed = state.seed;
        row.roundsPlayed = state.roundsPlayed;
        row.started = state.started;
        row.rulesVersion = rulesVersion;
        return row;
    }

    EntrantRow ToRow(Entrant const& e)
    {
        EntrantRow r;
        r.id = e.id;
        r.name = Cut(e.name, kNameBytes);
        r.faction = FactionCode(e.faction);
        r.heroClass = Cut(e.heroClass, kClassBytes);
        r.deckKey = Cut(e.deckKey, kDeckKeyBytes);
        r.profile = Match::AiProfileCode(e.profile);
        r.enabled = e.enabled;
        r.joinedRound = e.joinedRound;
        r.rank = static_cast<uint32_t>(e.rank);
        r.stars = static_cast<uint32_t>(e.stars);
        r.rating = e.rating;
        r.games = e.games;
        r.wins = e.wins;
        r.losses = e.losses;
        r.draws = e.draws;
        r.winStreak = static_cast<uint32_t>(std::max(0, e.winStreak));
        r.bestRank = static_cast<uint32_t>(e.bestRank);
        r.bestStars = static_cast<uint32_t>(e.bestStars);
        r.byes = e.byes;
        r.lastOpponent = e.lastOpponent;
        return r;
    }

    VsRow ToRow(VsRecord const& vs)
    {
        VsRow r;
        r.player = vs.player;
        r.npc = vs.npc;
        r.wins = vs.wins;
        r.losses = vs.losses;
        r.draws = vs.draws;
        return r;
    }

    std::vector<GameRow> ToGameRows(RoundPlan const& plan, std::vector<GameResult> const& results)
    {
        std::vector<GameRow> rows;
        for (GameResult const& g : results)
        {
            GameRow r;
            r.round = plan.round;
            r.slot = g.plan.slot;
            r.ratingOnly = plan.kind == RoundKind::RatingOnly;
            r.a = g.plan.a;
            r.b = g.plan.b;
            r.seed = g.plan.seed;
            r.outcome = SimOutcomeCode(g.outcome);
            r.firstSide = g.firstSide;
            r.turns = static_cast<uint32_t>(std::max(0, g.turns));
            r.error = Cut(g.error, kErrorBytes);
            rows.push_back(r);
        }
        return rows;
    }

    bool FromRow(EntrantRow const& row, Entrant& out, std::string& error)
    {
        Entrant e;
        e.id = row.id;
        e.name = row.name;
        if (!ParseFaction(row.faction, e.faction))
        {
            error = "entrant " + U(row.id) + ": unknown faction '" + row.faction + "'";
            return false;
        }
        if (!ParseProfile(row.profile, e.profile))
        {
            error = "entrant " + U(row.id) + ": unknown profile '" + row.profile + "'";
            return false;
        }
        if (row.rank > static_cast<uint32_t>(kBottomRank) || row.bestRank > static_cast<uint32_t>(kBottomRank) ||
            row.stars > static_cast<uint32_t>(MaxStars(static_cast<int>(row.rank))) ||
            row.bestStars > static_cast<uint32_t>(MaxStars(static_cast<int>(row.bestRank))))
        {
            error = "entrant " + U(row.id) + ": rank " + U(row.rank) + " with " + U(row.stars) + " stars (best " +
                U(row.bestRank) + " with " + U(row.bestStars) + ") is not on the ladder";
            return false;
        }
        e.heroClass = row.heroClass;
        e.deckKey = row.deckKey;
        e.enabled = row.enabled;
        e.joinedRound = row.joinedRound;
        e.rank = static_cast<int>(row.rank);
        e.stars = static_cast<int>(row.stars);
        e.rating = row.rating;
        e.games = row.games;
        e.wins = row.wins;
        e.losses = row.losses;
        e.draws = row.draws;
        e.winStreak = static_cast<int>(std::min<uint32_t>(row.winStreak, 1000000u));
        e.bestRank = static_cast<int>(row.bestRank);
        e.bestStars = static_cast<int>(row.bestStars);
        e.byes = row.byes;
        e.lastOpponent = row.lastOpponent;
        out = e;
        return true;
    }

    VsRecord FromRow(VsRow const& row)
    {
        VsRecord v;
        v.player = row.player;
        v.npc = row.npc;
        v.wins = row.wins;
        v.losses = row.losses;
        v.draws = row.draws;
        return v;
    }

    bool BuildState(LadderRow const& ladder, std::vector<EntrantRow> const& entrants, std::vector<VsRow> const& vs,
                    LadderState& out, std::string& error)
    {
        LadderState s;
        s.seed = ladder.seed;
        s.roundsPlayed = ladder.roundsPlayed;
        s.started = ladder.started;
        std::set<EntrantId> seen;
        for (EntrantRow const& row : entrants)
        {
            Entrant e;
            if (!FromRow(row, e, error))
                return false;
            if (!seen.insert(e.id).second)
            {
                error = "entrant " + U(e.id) + " is stored twice";
                return false;
            }
            s.entrants.push_back(e);
        }
        for (VsRow const& row : vs)
            s.vs.push_back(FromRow(row));
        out = s;
        return true;
    }

    OfferStateRow ToStateRow(OfferSet const& set)
    {
        OfferStateRow r;
        r.account = set.account;
        r.drawCounter = set.drawCounter;
        r.drawnRound = set.round;
        r.lastOpponent = set.lastOpponent;
        return r;
    }

    std::vector<OfferRow> ToOfferRows(OfferSet const& set)
    {
        std::vector<OfferRow> rows;
        for (std::size_t i = 0; i < set.slots.size() && i < static_cast<std::size_t>(kOfferCount); ++i)
        {
            OfferRow r;
            r.account = set.account;
            r.slot = static_cast<uint32_t>(i);
            r.npc = set.slots[i];
            rows.push_back(r);
        }
        return rows;
    }

    bool BuildOfferSet(OfferStateRow const& state, std::vector<OfferRow> const& slots, OfferSet& out, std::string& error)
    {
        OfferSet s;
        s.account = state.account;
        s.drawCounter = state.drawCounter;
        s.round = state.drawnRound;
        s.lastOpponent = state.lastOpponent;
        std::vector<uint64_t> bySlot(static_cast<std::size_t>(kOfferCount), 0);
        std::vector<char> used(static_cast<std::size_t>(kOfferCount), 0);
        std::size_t count = 0;
        for (OfferRow const& row : slots)
        {
            if (row.account != state.account)
            {
                error = "offer of account " + U(row.account) + " stored with the offers of account " + U(state.account);
                return false;
            }
            if (row.slot >= static_cast<uint32_t>(kOfferCount))
            {
                error = "account " + U(state.account) + ": offer slot " + U(row.slot) + " does not exist";
                return false;
            }
            if (used[row.slot])
            {
                error = "account " + U(state.account) + ": offer slot " + U(row.slot) + " is stored twice";
                return false;
            }
            used[row.slot] = 1;
            bySlot[row.slot] = row.npc;
            ++count;
        }
        for (std::size_t i = 0; i < count; ++i)
            if (!used[i])
            {
                error = "account " + U(state.account) + ": offer slot " + U(i) + " is missing";
                return false;
            }
        for (std::size_t i = 0; i < count; ++i)
            s.slots.push_back(bySlot[i]);
        out = s;
        return true;
    }

    // ---- cells -> rows -----------------------------------------------------------------------------------

    bool LadderRowFromCells(std::vector<std::string> const& c, LadderRow& out)
    {
        uint32_t id = 0, started = 0;
        LadderRow r;
        if (c.size() != kLadderColumnCount || !ParseU32(c[0], id) || id != 1 || !ParseU32(c[1], r.seed) ||
            !ParseU32(c[2], r.roundsPlayed) || !ParseU32(c[3], started) || started > 1 || !ParseU32(c[4], r.rulesVersion))
            return false;
        r.started = started != 0;
        out = r;
        return true;
    }

    bool EntrantRowFromCells(std::vector<std::string> const& c, EntrantRow& out)
    {
        EntrantRow r;
        uint32_t enabled = 0;
        if (c.size() != kEntrantColumnCount || !ParseU64(c[0], r.id) || !ParseU32(c[6], enabled) || enabled > 1 ||
            !ParseU32(c[7], r.joinedRound) || !ParseU32(c[8], r.rank) || !ParseU32(c[9], r.stars) ||
            !ParseI32(c[10], r.rating) || !ParseU32(c[11], r.games) || !ParseU32(c[12], r.wins) ||
            !ParseU32(c[13], r.losses) || !ParseU32(c[14], r.draws) || !ParseU32(c[15], r.winStreak) ||
            !ParseU32(c[16], r.bestRank) || !ParseU32(c[17], r.bestStars) || !ParseU32(c[18], r.byes) ||
            !ParseU64(c[19], r.lastOpponent))
            return false;
        r.name = c[1];
        r.faction = c[2];
        r.heroClass = c[3];
        r.deckKey = c[4];
        r.profile = c[5];
        r.enabled = enabled != 0;
        out = r;
        return true;
    }

    bool VsRowFromCells(std::vector<std::string> const& c, VsRow& out)
    {
        VsRow r;
        if (c.size() != kVsColumnCount || !ParseU64(c[0], r.player) || !ParseU64(c[1], r.npc) ||
            !ParseU32(c[2], r.wins) || !ParseU32(c[3], r.losses) || !ParseU32(c[4], r.draws))
            return false;
        out = r;
        return true;
    }

    bool GameRowFromCells(std::vector<std::string> const& c, GameRow& out)
    {
        GameRow r;
        uint32_t ratingOnly = 0;
        int32_t first = 0;
        if (c.size() != kGameColumnCount || !ParseU32(c[0], r.round) || !ParseU32(c[1], r.slot) ||
            !ParseU32(c[2], ratingOnly) || ratingOnly > 1 || !ParseU64(c[3], r.a) || !ParseU64(c[4], r.b) ||
            !ParseU32(c[5], r.seed) || !ParseI32(c[7], first) || first < -1 || first > 1 || !ParseU32(c[8], r.turns))
            return false;
        r.ratingOnly = ratingOnly != 0;
        r.outcome = c[6];
        r.firstSide = first;
        r.error = c[9];
        out = r;
        return true;
    }

    bool OfferRowFromCells(std::vector<std::string> const& c, OfferRow& out)
    {
        OfferRow r;
        if (c.size() != kOfferColumnCount || !ParseU32(c[0], r.account) || !ParseU32(c[1], r.slot) ||
            !ParseU64(c[2], r.npc))
            return false;
        out = r;
        return true;
    }

    bool OfferStateRowFromCells(std::vector<std::string> const& c, OfferStateRow& out)
    {
        OfferStateRow r;
        if (c.size() != kOfferStateColumnCount || !ParseU32(c[0], r.account) || !ParseU32(c[1], r.drawCounter) ||
            !ParseU32(c[2], r.drawnRound) || !ParseU64(c[3], r.lastOpponent))
            return false;
        out = r;
        return true;
    }

    // ---- statements ---------------------------------------------------------------------------------------

    std::string LadderUpsertSql(LadderRow const& row)
    {
        return std::string("INSERT INTO `") + kTableLadder + "` (" + kLadderColumns + ") VALUES (1, " + U(row.seed) + ", " +
            U(row.roundsPlayed) + ", " + (row.started ? "1" : "0") + ", " + U(row.rulesVersion) +
            ") ON DUPLICATE KEY UPDATE `seed` = " + U(row.seed) + ", `rounds_played` = " + U(row.roundsPlayed) +
            ", `started` = " + (row.started ? "1" : "0") + ", `rules_version` = " + U(row.rulesVersion);
    }

    std::vector<std::string> EntrantReplaceSql(std::vector<EntrantRow> const& rows, SqlEscape const& escape)
    {
        std::vector<std::string> tuples;
        for (EntrantRow const& r : rows)
            tuples.push_back("(" + U(r.id) + ", " + Quote(escape, r.name) + ", " + Quote(escape, r.faction) + ", " +
                Quote(escape, r.heroClass) + ", " + Quote(escape, r.deckKey) + ", " + Quote(escape, r.profile) + ", " +
                (r.enabled ? "1" : "0") + ", " + U(r.joinedRound) + ", " + U(r.rank) + ", " + U(r.stars) + ", " +
                S(r.rating) + ", " + U(r.games) + ", " + U(r.wins) + ", " + U(r.losses) + ", " + U(r.draws) + ", " +
                U(r.winStreak) + ", " + U(r.bestRank) + ", " + U(r.bestStars) + ", " + U(r.byes) + ", " +
                U(r.lastOpponent) + ")");
        return ReplaceChunks(kTableEntrant, kEntrantColumns, tuples);
    }

    std::vector<std::string> VsReplaceSql(std::vector<VsRow> const& rows)
    {
        std::vector<std::string> tuples;
        for (VsRow const& r : rows)
            tuples.push_back("(" + U(r.player) + ", " + U(r.npc) + ", " + U(r.wins) + ", " + U(r.losses) + ", " +
                U(r.draws) + ")");
        return ReplaceChunks(kTableVs, kVsColumns, tuples);
    }

    std::vector<std::string> GameReplaceSql(std::vector<GameRow> const& rows, SqlEscape const& escape)
    {
        std::vector<std::string> tuples;
        for (GameRow const& r : rows)
            tuples.push_back("(" + U(r.round) + ", " + U(r.slot) + ", " + (r.ratingOnly ? "1" : "0") + ", " + U(r.a) + ", " +
                U(r.b) + ", " + U(r.seed) + ", " + Quote(escape, r.outcome) + ", " + S(r.firstSide) + ", " + U(r.turns) +
                ", " + Quote(escape, r.error) + ")");
        return ReplaceChunks(kTableGame, kGameColumns, tuples);
    }

    std::vector<std::string> ResetSql()
    {
        std::vector<std::string> out;
        for (char const* table : { kTableGame, kTableVs, kTableEntrant, kTableLadder })
            out.push_back(std::string("DELETE FROM `") + table + "`");
        return out;
    }

    namespace
    {
        // The entrant rows of the ids (an id the state does not hold is skipped), in id order.
        std::vector<EntrantRow> EntrantRowsOf(LadderState const& state, std::vector<EntrantId> const& ids)
        {
            std::set<EntrantId> const wanted(ids.begin(), ids.end());
            std::vector<EntrantRow> rows;
            for (EntrantId id : wanted)
                if (Entrant const* e = state.Find(id))
                    rows.push_back(ToRow(*e));
            return rows;
        }

        void Append(std::vector<std::string>& to, std::vector<std::string> const& more)
        {
            to.insert(to.end(), more.begin(), more.end());
        }
    }

    std::vector<std::string> RosterSaveSql(LadderState const& state, std::vector<EntrantId> const& changed,
                                           uint32_t rulesVersion, SqlEscape const& escape)
    {
        std::vector<std::string> sql;
        sql.push_back(LadderUpsertSql(ToLadderRow(state, rulesVersion)));
        Append(sql, EntrantReplaceSql(EntrantRowsOf(state, changed), escape));
        return sql;
    }

    std::vector<std::string> RoundSaveSql(LadderState const& state, RoundPlan const& plan,
                                          std::vector<GameResult> const& results, RoundReport const& report,
                                          uint32_t rulesVersion, SqlEscape const& escape)
    {
        std::vector<std::string> sql = RosterSaveSql(state, report.touched, rulesVersion, escape);
        Append(sql, GameReplaceSql(ToGameRows(plan, results), escape));
        return sql;
    }

    std::vector<std::string> PlacementSaveSql(LadderState const& state, uint32_t rulesVersion, SqlEscape const& escape)
    {
        std::vector<EntrantId> all;
        for (Entrant const& e : state.entrants)
            all.push_back(e.id);
        return RosterSaveSql(state, all, rulesVersion, escape);
    }

    std::vector<std::string> PlayerResultSaveSql(LadderState const& state, EntrantId player, EntrantId npc,
                                                 std::vector<EntrantId> const& pushedOut, uint32_t rulesVersion,
                                                 SqlEscape const& escape)
    {
        std::vector<EntrantId> ids = pushedOut;
        ids.push_back(player);
        ids.push_back(npc);
        std::vector<VsRow> vs;
        if (VsRecord const* record = state.FindVs(player, npc))
            vs.push_back(ToRow(*record));
        std::vector<std::string> sql = RosterSaveSql(state, ids, rulesVersion, escape);
        Append(sql, VsReplaceSql(vs));
        return sql;
    }

    std::vector<std::string> OfferSaveSql(OfferSet const& set)
    {
        OfferStateRow const state = ToStateRow(set);
        std::vector<std::string> sql;
        sql.push_back(std::string("DELETE FROM `") + kTableOffer + "` WHERE `account_id` = " + U(set.account));
        std::vector<std::string> tuples;
        for (OfferRow const& r : ToOfferRows(set))
            tuples.push_back("(" + U(r.account) + ", " + U(r.slot) + ", " + U(r.npc) + ")");
        Append(sql, ReplaceChunks(kTableOffer, kOfferColumns, tuples));
        sql.push_back(std::string("INSERT INTO `") + kTableOfferState + "` (" + kOfferStateColumns + ") VALUES (" +
            U(state.account) + ", " + U(state.drawCounter) + ", " + U(state.drawnRound) + ", " + U(state.lastOpponent) +
            ") ON DUPLICATE KEY UPDATE `draw_counter` = " + U(state.drawCounter) + ", `drawn_round` = " + U(state.drawnRound) +
            ", `last_opponent` = " + U(state.lastOpponent));
        return sql;
    }

    std::vector<std::string> OfferResetSql()
    {
        std::vector<std::string> out;
        for (char const* table : { kTableOffer, kTableOfferState })
            out.push_back(std::string("DELETE FROM `") + table + "`");
        return out;
    }

    // ---- text -----------------------------------------------------------------------------------------------

    std::string RatingText(int32_t centiPoints)
    {
        int64_t const v = centiPoints;
        int64_t const a = v < 0 ? -v : v;
        std::string cents = std::to_string(a % 100);
        if (cents.size() < 2)
            cents = "0" + cents;
        return std::string(v < 0 ? "-" : "") + std::to_string(a / 100) + "." + cents;
    }

    std::string RankText(int rank, int stars)
    {
        if (rank == kLegend)
            return "Legend";
        return "Rank " + std::to_string(rank) + ", " + std::to_string(stars) + (stars == 1 ? " star" : " stars");
    }

    std::string ClassText(std::string const& heroClass)
    {
        std::string s = heroClass;
        for (std::size_t i = 0; i < s.size(); ++i)
            s[i] = static_cast<char>(i == 0 ? std::toupper(static_cast<unsigned char>(s[i]))
                                            : std::tolower(static_cast<unsigned char>(s[i])));
        return s;
    }
}
}
