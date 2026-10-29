#include "HsMatchHistory.h"

#include "HsMatch.h"
#include "HsMatchService.h"

#include "../HsDataset.h"

#include <algorithm>
#include <map>

namespace Hs
{
    std::string EncodeCardList(std::vector<std::string> const& cards)
    {
        std::map<std::string, uint32_t> counts;
        for (std::string const& card : cards)
            if (!card.empty())
                ++counts[card];
        std::string out;
        for (auto const& entry : counts)
        {
            if (!out.empty())
                out += ',';
            out += entry.first + "." + std::to_string(entry.second);
        }
        return out;
    }

    std::string HistoryResultOf(std::string const& res, std::string const& why)
    {
        if (res == "win" || res == "draw")
            return res;
        if (res == "loss")
            return why == "concede" ? "surrender" : "loss";
        if (res == "int")
            return "interrupted";
        return "unknown";
    }

    std::string ClipHistoryText(std::string const& text, std::size_t maxBytes)
    {
        std::string out;
        std::size_t i = 0;
        while (i < text.size())
        {
            unsigned char const c = static_cast<unsigned char>(text[i]);
            std::size_t len = 1;
            if (c >= 0xF0)
                len = 4;
            else if (c >= 0xE0)
                len = 3;
            else if (c >= 0xC0)
                len = 2;
            bool valid = c < 0x80 || (c >= 0xC2 && c < 0xF0);
            if (i + len > text.size())
            {
                len = text.size() - i; // a truncated sequence: replaced as one piece
                valid = false;
            }
            for (std::size_t k = 1; valid && k < len; ++k)
                valid = (static_cast<unsigned char>(text[i + k]) & 0xC0) == 0x80;
            // no overlong 3-byte forms and no UTF-16 surrogates (E0 needs A0..BF, ED needs 80..9F)
            if (valid && len == 3)
            {
                unsigned char const c1 = static_cast<unsigned char>(text[i + 1]);
                valid = !(c == 0xE0 && c1 < 0xA0) && !(c == 0xED && c1 > 0x9F);
            }
            if (!valid && c >= 0x80 && c < 0xC0)
                len = 1; // a stray continuation byte
            else if (!valid && len > 1 && i + len <= text.size())
            {
                // a lead byte with bad continuation bytes: replace only the lead byte and look at the rest again
                bool broken = false;
                for (std::size_t k = 1; k < len; ++k)
                    if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80)
                        broken = true;
                if (broken)
                    len = 1;
            }
            std::string piece;
            if (c < 0x20 || c == 0x7F)
                piece = " ";
            else if (c >= 0xF0 || !valid)
                piece = "?"; // 4-byte characters (the utf8mb3 connection) and invalid UTF-8
            else
                piece = text.substr(i, len);
            if (out.size() + piece.size() > maxBytes)
                break;
            out += piece;
            i += len;
        }
        return out;
    }

    std::string FormatHistoryDuration(uint32_t seconds)
    {
        if (seconds >= 3600)
        {
            uint32_t const minutes = (seconds % 3600) / 60;
            return std::to_string(seconds / 3600) + "h" + (minutes < 10 ? "0" : "") + std::to_string(minutes) + "m";
        }
        if (seconds >= 60)
            return std::to_string(seconds / 60) + "m" + ((seconds % 60) < 10 ? "0" : "") + std::to_string(seconds % 60) + "s";
        return std::to_string(seconds) + "s";
    }

    std::string FormatMatchHistoryLine(MatchHistoryListing const& entry)
    {
        MatchHistoryRow const& r = entry.row;
        std::string out = "  #" + std::to_string(entry.id) + " " + entry.endedText + " " + r.characterName + " vs " +
            r.opponentName + " [" + r.difficulty + "], deck \"" + r.deckName + "\" (" + r.deckClass + ") vs " +
            (r.opponentDeckKey.empty() ? std::string("?") : r.opponentDeckKey) + ": " + r.result;
        if (!r.endReason.empty())
            out += " (" + r.endReason + ")";
        out += ", " + std::to_string(r.turns) + (r.turns == 1 ? " turn, " : " turns, ") +
            FormatHistoryDuration(r.durationSec) + ", seed " + std::to_string(r.seed);
        return out;
    }

    MatchHistoryRow BuildMatchHistoryRow(MatchEnd const& end, int64_t endedAt)
    {
        MatchMeta const& meta = end.meta;
        MatchHistoryRow row;
        row.accountId = meta.accountId;
        row.characterGuid = end.guid;
        row.characterName = ClipHistoryText(end.humanName, kHistoryCharNameBytes);
        row.matchId = end.matchId;
        row.opponentEntry = meta.opponentEntry;
        row.opponentName = ClipHistoryText(end.opponentName, kHistoryNameBytes);
        row.difficulty = ClipHistoryText(end.difficulty, kHistoryCodeBytes);
        row.deckId = meta.deckId;
        row.deckName = ClipHistoryText(meta.deckName, kHistoryNameBytes);
        row.deckClass = ClipHistoryText(meta.humanClass, kHistoryClassBytes);
        row.opponentDeckKey = ClipHistoryText(meta.opponentDeckKey, kHistoryKeyBytes);
        row.opponentClass = ClipHistoryText(meta.opponentClass, kHistoryClassBytes);
        row.result = HistoryResultOf(end.res, end.why);
        row.endReason = ClipHistoryText(end.why, kHistoryCodeBytes);
        row.turns = end.turn > 0 ? static_cast<uint32_t>(end.turn) : 0;
        row.endedAt = endedAt;
        // The clock may have been set back between the start and the end: never a negative duration.
        int64_t const span = meta.startedAt > 0 && endedAt > meta.startedAt ? endedAt - meta.startedAt : 0;
        row.durationSec = static_cast<uint32_t>(std::min<int64_t>(span, 0xFFFFFFFFLL));
        row.startedAt = meta.startedAt > 0 ? meta.startedAt : endedAt;
        row.seed = meta.seed;
        row.datasetId = ClipHistoryText(kDatasetId, 32);
        row.datasetHash = kDatasetHash;
        row.rulesVersion = static_cast<uint32_t>(Match::kRulesVersion);
        row.aiDelayMs = meta.aiDelayMs;
        row.humanCards = ClipHistoryText(meta.humanCards, kHistoryCardsBytes);
        row.opponentCards = ClipHistoryText(meta.opponentCards, kHistoryCardsBytes);
        row.currencyPaid = 0;
        return row;
    }
}
