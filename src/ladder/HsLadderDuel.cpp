#include "HsLadderDuel.h"

#include "../match/HsMatchReplay.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace Hs
{
namespace Ladder
{
    namespace
    {
        std::string U(uint64_t v) { return std::to_string(v); }

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

        std::string Quote(SqlEscape const& escape, std::string const& text)
        {
            return "'" + escape(text) + "'";
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

        bool ParseI64(std::string const& text, int64_t& out)
        {
            bool const negative = !text.empty() && text[0] == '-';
            uint64_t v = 0;
            if (!ParseU64(negative ? text.substr(1) : text, v) || v > static_cast<uint64_t>(INT64_MAX))
                return false;
            out = negative ? -static_cast<int64_t>(v) : static_cast<int64_t>(v);
            return true;
        }

        bool CardCharOk(char c)
        {
            return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '-';
        }

        bool StateOk(std::string const& state)
        {
            return state == kDuelActive || state == kDuelPaused;
        }

        bool ProfileOk(std::string const& profile)
        {
            return profile == "normal" || profile == "hard";
        }
    }

    bool DuelReplayable(DuelRow const& row, uint32_t rulesVersion, std::string const& datasetHash)
    {
        return row.rulesVersion == rulesVersion && row.datasetHash == datasetHash;
    }

    std::string JoinCards(std::vector<std::string> const& cards)
    {
        std::string out;
        for (std::size_t i = 0; i < cards.size(); ++i)
            out += (i ? "," : "") + cards[i];
        return out;
    }

    bool SplitCards(std::string const& text, std::vector<std::string>& out)
    {
        std::vector<std::string> cards;
        std::string current;
        auto const flush = [&]() { cards.push_back(current); current.clear(); };
        for (char c : text)
        {
            if (c == ',')
            {
                if (current.empty())
                    return false;
                flush();
            }
            else if (CardCharOk(c))
                current.push_back(c);
            else
                return false;
        }
        if (!text.empty())
        {
            if (current.empty())
                return false;
            flush();
        }
        out = std::move(cards);
        return true;
    }

    DuelRow DuelFromParams(MatchStartParams const& params, uint64_t id, uint32_t npcEntry, uint32_t rulesVersion,
                           std::string const& datasetHash)
    {
        DuelRow row;
        row.id = id;
        row.account = params.meta.accountId;
        row.guid = params.guid;
        row.state = kDuelActive;
        row.npcEntry = npcEntry;
        row.opponentName = Cut(params.opponentName, kNameBytes);
        row.opponentClass = Cut(params.opponentClass, kClassBytes);
        row.opponentDeckKey = Cut(params.meta.opponentDeckKey, kDeckKeyBytes);
        row.opponentDeck = params.opponentDeck;
        row.profile = Match::AiProfileCode(params.profile);
        row.deckId = params.meta.deckId;
        row.deckName = Cut(params.meta.deckName, kNameBytes);
        row.humanClass = Cut(params.humanClass, kClassBytes);
        row.humanDeck = params.humanDeck;
        row.seed = params.seed;
        row.startedAt = params.meta.startedAt;
        row.rulesVersion = rulesVersion;
        row.datasetHash = datasetHash;
        row.timerMs = 0;
        return row;
    }

    MatchStartParams ParamsFromDuel(DuelRow const& row, uint32_t guid, std::string const& humanName)
    {
        MatchStartParams params;
        params.guid = guid;
        params.humanName = humanName;
        params.humanClass = row.humanClass;
        params.humanDeck = row.humanDeck;
        params.opponentName = row.opponentName;
        params.opponentClass = row.opponentClass;
        params.opponentDeck = row.opponentDeck;
        params.seed = row.seed;
        params.profile = row.profile == "hard" ? Match::AiProfile::Hard : Match::AiProfile::Normal;
        params.difficulty = row.profile == "hard" ? "hard" : "normal";
        params.meta.accountId = row.account;
        params.meta.opponentEntry = row.npcEntry;
        params.meta.deckId = row.deckId;
        params.meta.deckName = row.deckName;
        params.meta.opponentDeckKey = row.opponentDeckKey;
        params.meta.startedAt = row.startedAt;
        params.arena = true;
        params.arenaRunId = RunOfDuel(row.id);
        params.ladder = true;
        return params;
    }

    SuspendedDuel SuspendedFromDuel(DuelRow const& row, uint32_t guid, std::string const& humanName,
                                    std::vector<LoggedAction> const& log)
    {
        SuspendedDuel duel;
        duel.params = ParamsFromDuel(row, guid, humanName);
        duel.log = log;
        duel.timerMs = row.timerMs;
        duel.rulesVersion = static_cast<int>(row.rulesVersion);
        duel.datasetHash = row.datasetHash;
        return duel;
    }

    bool DecodeDuelLog(std::vector<std::string> const& rows, std::vector<LoggedAction>& out, std::size_t& failedAt,
                       std::string& error)
    {
        std::vector<LoggedAction> log;
        log.reserve(rows.size());
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            LoggedAction step;
            if (!DecodeAction(rows[i], step, error))
            {
                failedAt = i;
                return false;
            }
            log.push_back(std::move(step));
        }
        out = std::move(log);
        return true;
    }

    bool DuelRowFromCells(std::vector<std::string> const& cells, DuelRow& out)
    {
        if (cells.size() != kDuelColumnCount)
            return false;
        DuelRow r;
        if (!ParseU64(cells[0], r.id) || r.id == 0 || IsLadderRun(r.id) || !ParseU32(cells[1], r.account) || !ParseU32(cells[2], r.guid))
            return false;
        r.state = cells[3];
        if (!StateOk(r.state) || !ParseU32(cells[4], r.npcEntry))
            return false;
        r.opponentName = cells[5];
        r.opponentClass = cells[6];
        r.opponentDeckKey = cells[7];
        if (!SplitCards(cells[8], r.opponentDeck))
            return false;
        r.profile = cells[9];
        if (!ProfileOk(r.profile) || !ParseU32(cells[10], r.deckId))
            return false;
        r.deckName = cells[11];
        r.humanClass = cells[12];
        if (!SplitCards(cells[13], r.humanDeck) || !ParseU32(cells[14], r.seed) || !ParseI64(cells[15], r.startedAt) ||
            !ParseU32(cells[16], r.rulesVersion))
            return false;
        r.datasetHash = cells[17];
        if (!ParseU32(cells[18], r.timerMs))
            return false;
        out = std::move(r);
        return true;
    }

    bool QueueRowFromCells(std::vector<std::string> const& cells, QueueRow& out)
    {
        if (cells.size() != kQueueColumnCount)
            return false;
        QueueRow r;
        uint64_t kind = 0, result = 0;
        if (!ParseU64(cells[0], r.seq) || !ParseU64(cells[1], kind) || !ParseU64(cells[2], r.excludedNpc) ||
            !ParseU32(cells[3], r.account) || !ParseU64(cells[5], r.npcId) || !ParseU64(cells[6], result) ||
            !ParseU64(cells[7], r.duelId))
            return false;
        if ((kind != kQueueRound && kind != kQueueResult) || result > 2)
            return false;
        r.kind = static_cast<uint32_t>(kind);
        r.result = static_cast<uint8_t>(result);
        r.playerName = cells[4];
        out = r;
        return true;
    }

    std::string DuelInsertSql(DuelRow const& r, SqlEscape const& escape)
    {
        return std::string("INSERT INTO `") + kTableDuel + "` (" + kDuelColumns + ") VALUES (" + U(r.id) + ", " + U(r.account) +
            ", " + U(r.guid) + ", " + Quote(escape, r.state) + ", " + U(r.npcEntry) + ", " + Quote(escape, r.opponentName) +
            ", " + Quote(escape, r.opponentClass) + ", " + Quote(escape, r.opponentDeckKey) + ", " +
            Quote(escape, JoinCards(r.opponentDeck)) + ", " + Quote(escape, r.profile) + ", " + U(r.deckId) + ", " +
            Quote(escape, r.deckName) + ", " + Quote(escape, r.humanClass) + ", " + Quote(escape, JoinCards(r.humanDeck)) +
            ", " + U(r.seed) + ", " + std::to_string(r.startedAt) + ", " + U(r.rulesVersion) + ", " +
            Quote(escape, r.datasetHash) + ", " + U(r.timerMs) + ")";
    }

    std::string DuelActionsInsertSql(uint64_t duelId, uint32_t first, std::vector<LoggedAction> const& actions, bool& ok)
    {
        ok = true;
        if (actions.empty())
            return std::string();
        std::string values;
        for (std::size_t i = 0; i < actions.size(); ++i)
        {
            std::string const text = EncodeAction(actions[i]);
            if (text.empty() || text.size() > kDuelActionBytes)
            {
                ok = false;
                return std::string();
            }
            // The encoded text is letters, digits, commas and minus signs: nothing to escape.
            values += (i ? ", (" : "(") + U(duelId) + ", " + U(first + static_cast<uint32_t>(i)) + ", '" + text + "')";
        }
        return std::string("INSERT INTO `") + kTableDuelAction + "` (`duel_id`, `idx`, `action`) VALUES " + values;
    }

    std::vector<std::string> DuelPauseSql(uint64_t duelId, uint32_t timerMs, bool trim, uint32_t keptActions)
    {
        std::vector<std::string> out;
        if (trim)
            out.push_back(std::string("DELETE FROM `") + kTableDuelAction + "` WHERE `duel_id` = " + U(duelId) +
                " AND `idx` >= " + U(keptActions));
        out.push_back(std::string("UPDATE `") + kTableDuel + "` SET `state` = '" + kDuelPaused + "', `timer_ms` = " + U(timerMs) +
            " WHERE `duel_id` = " + U(duelId));
        return out;
    }

    std::string DuelActiveSql(uint64_t duelId, uint32_t guid)
    {
        return std::string("UPDATE `") + kTableDuel + "` SET `state` = '" + kDuelActive + "', `guid` = " + U(guid) +
            ", `timer_ms` = 0 WHERE `duel_id` = " + U(duelId);
    }

    std::vector<std::string> DuelDeleteSql(uint64_t duelId)
    {
        return {
            std::string("DELETE FROM `") + kTableDuelAction + "` WHERE `duel_id` = " + U(duelId),
            std::string("DELETE FROM `") + kTableDuel + "` WHERE `duel_id` = " + U(duelId),
        };
    }

    std::vector<std::string> SettleSql(uint64_t duelId)
    {
        std::vector<std::string> out;
        if (duelId != 0)
            out = DuelDeleteSql(duelId);
        return out;
    }

    std::vector<std::string> QueueSnapshotSql(std::vector<QueueRow> const& rows, SqlEscape const& escape)
    {
        std::vector<std::string> out;
        out.push_back(std::string("DELETE FROM `") + kTableQueue + "`");
        if (rows.empty())
            return out;
        std::string values;
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            QueueRow const& r = rows[i];
            values += (i ? ", (" : "(") + U(r.kind) + ", " + U(r.excludedNpc) + ", " + U(r.account) + ", " +
                Quote(escape, r.playerName) + ", " + U(r.npcId) + ", " + U(static_cast<uint32_t>(r.result)) + ", " +
                U(r.duelId) + ")";
        }
        out.push_back(std::string("INSERT INTO `") + kTableQueue + "` (`kind`, `excluded_npc`, `account_id`, `player_name`, " +
            "`npc_id`, `result`, `duel_id`) VALUES " + values);
        return out;
    }

    std::vector<std::string> DuelResetSql()
    {
        return {
            std::string("DELETE FROM `") + kTableDuelAction + "`",
            std::string("DELETE FROM `") + kTableDuel + "`",
            std::string("DELETE FROM `") + kTableQueue + "`",
        };
    }

    std::vector<std::string> MilestonesFor(int rank, bool first)
    {
        std::vector<std::string> out;
        if (rank == kLegend)
        {
            out.push_back(kMilestoneLegend);
            if (first)
                out.push_back(kMilestoneLegendFirst);
        }
        return out;
    }

    std::string MilestoneInsertSql(uint32_t account, std::string const& milestone)
    {
        // The names are the constants above: letters and digits only.
        return std::string("INSERT IGNORE INTO `") + kTableMilestone + "` (`account_id`, `milestone`) VALUES (" + U(account) +
            ", '" + milestone + "')";
    }

    std::string MilestoneText(std::string const& milestone)
    {
        if (milestone == kMilestoneLegend)
            return "You reached Legend.";
        if (milestone == kMilestoneLegendFirst)
            return "You are the number one of Legend.";
        return std::string();
    }

    std::string DuelText(DuelRow const& row, uint32_t actions)
    {
        return std::string(row.state == kDuelPaused ? "paused" : "running") + " duel against " + row.opponentName + " (" +
            ClassText(row.opponentClass) + ", " + std::to_string(actions) + " action(s))";
    }
}
}
