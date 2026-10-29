#include "HsArenaRows.h"

#include <utility>

namespace Hs
{
    namespace
    {
        bool IdOk(std::string const& id)
        {
            if (id.empty() || id.size() > kMaxCardIdLen)
                return false;
            for (char c : id)
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
                    return false;
            return true;
        }
    }

    std::string EncodeArenaIds(std::vector<std::string> const& ids)
    {
        std::string text;
        for (std::string const& id : ids)
        {
            if (!text.empty())
                text.push_back(',');
            text += id;
        }
        return text;
    }

    bool DecodeArenaIds(std::string const& text, std::size_t maxCount, std::vector<std::string>& out)
    {
        std::vector<std::string> ids;
        if (!text.empty())
        {
            std::size_t start = 0;
            while (true)
            {
                std::size_t const comma = text.find(',', start);
                std::string const id = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                if (!IdOk(id) || ids.size() >= maxCount)
                    return false;
                ids.push_back(id);
                if (comma == std::string::npos)
                    break;
                start = comma + 1;
            }
        }
        out = std::move(ids);
        return true;
    }

    bool ArenaOpponentOk(ArenaOpponent const& opponent, CardCatalog const& catalog, Match::CardDb const& db,
                         ArenaQuestSet const* questOwned)
    {
        if (opponent.name.empty() || opponent.name.size() > kArenaOpponentNameBytes)
            return false;
        return ValidateArenaDeck(catalog, db, opponent.deckClass, opponent.deck, questOwned) == ArenaDeckWhy::None;
    }

    MatchStartParams MakeArenaDuelParams(ArenaRun const& run, ArenaOpponent const& opponent, uint32_t guid,
                                         std::string const& humanName, uint32_t hostEntry)
    {
        MatchStartParams params;
        params.guid = guid;
        params.humanName = humanName;
        params.humanClass = run.deckClass;
        params.humanDeck = run.Deck();
        params.opponentName = opponent.name;
        params.opponentClass = opponent.deckClass;
        params.opponentDeck = opponent.deck;
        params.seed = opponent.seed;
        params.difficulty = kArenaDifficulty;
        params.profile = Match::AiProfile::Hard;
        params.meta.accountId = run.accountId;
        params.meta.opponentEntry = hostEntry;
        params.meta.deckName = "Arena run " + std::to_string(run.id);
        params.meta.opponentDeckKey = "arena:" + std::to_string(run.id) + "." + std::to_string(opponent.matchNo);
        params.meta.startedAt = opponent.startedAt;
        params.arena = true;
        params.arenaRunId = run.id;
        return params;
    }

    SuspendedDuel MakeSuspendedDuel(MatchStartParams const& params, std::vector<LoggedAction> const& log,
                                    ArenaOpponent const& opponent)
    {
        SuspendedDuel duel;
        duel.params = params;
        duel.log = log;
        duel.timerMs = opponent.timerMs;
        duel.rulesVersion = opponent.rulesVersion;
        duel.datasetHash = opponent.datasetHash;
        return duel;
    }

    bool DecodeArenaLog(std::vector<std::string> const& rows, std::vector<LoggedAction>& out, std::size_t& failedAt,
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

    bool ArenaDuelResultOf(std::string const& res, ArenaDuelResult& out)
    {
        if (res == "win")
            out = ArenaDuelResult::Win;
        else if (res == "draw")
            out = ArenaDuelResult::Draw;
        else if (res == "loss")
            out = ArenaDuelResult::Loss;
        else
            return false;
        return true;
    }

    char const* ArenaDuelResultCode(ArenaDuelResult result)
    {
        switch (result)
        {
            case ArenaDuelResult::Win: return "win";
            case ArenaDuelResult::Draw: return "draw";
            case ArenaDuelResult::Loss: return "loss";
        }
        return "loss";
    }
}
