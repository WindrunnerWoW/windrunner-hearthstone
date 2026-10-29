/*
 * Match history: the row that is written to hearthstone_match_history when a match ends.
 *
 * PURE C++ (standard library only, no core headers), unit tested in tests/protocol_tests.cpp. The
 * SQL (insert, the .hs match history query, the table check) is in src/HsMatchStore.cpp; the glue
 * calls it from HsMatchGlue.cpp at every end of a match.
 *
 * What a row holds, from the human's point of view (side 0):
 *   - identity: account id, character guid and name, match id (unique since the last server start
 *     only), the opponent creature entry and name, difficulty;
 *   - decks: the player's deck id, name and class, the opponent's deck key and class, and both card
 *     lists (<card id>.<copies>,... sorted by id, like hearthstone_deck.cards) so a match can be
 *     reproduced after the decks changed;
 *   - outcome: result (win, loss, draw, surrender, interrupted), end reason (hero, concede, turns,
 *     stuck, teleport, death, npc, gm, error, logout), turns, start and end time, duration;
 *   - reproducibility: the engine seed (the shuffle, the first player, every random effect and, through
 *     AiSeedFor(seed, seq), every AI decision), the engine rules version (Hs::Match::kRulesVersion),
 *     the card dataset id and hash, the AI action delay, the difficulty (it selects the AI profile);
 *   - currency_paid: always 0 (the match coins are paid by the economy glue, not recorded here).
 * The player's actions are not stored: with the same seed, decks, rules version and dataset the opponent
 * and all randomness are the same, and the actions of the player are the only free input. Not stored
 * either: a boss row's hero overrides (health, armor, hero card, hero power; hearthstone_opponent may
 * change later) and which turns a Nozdormu turn timer ended (wall-clock time).
 */

#ifndef MOD_HEARTHSTONE_HS_MATCH_HISTORY_H
#define MOD_HEARTHSTONE_HS_MATCH_HISTORY_H

#include <cstdint>
#include <string>
#include <vector>

namespace Hs
{
    struct MatchEnd; // HsMatchService.h

    // What the glue knows when a match starts and the service keeps until it ends (MatchStartParams::meta,
    // MatchEnd::meta). The service fills in the fields marked (service).
    struct MatchMeta
    {
        uint32_t accountId = 0;
        uint32_t opponentEntry = 0;    // the opponent creature entry (0 unknown)
        uint32_t deckId = 0;           // the player's saved deck (0 unknown)
        std::string deckName;
        std::string opponentDeckKey;   // hearthstone_deck_template.deck_key
        int64_t startedAt = 0;         // unix seconds (0 unknown)
        // (service)
        uint32_t seed = 0;             // the engine seed of the match
        uint32_t aiDelayMs = 0;        // the AI action delay in force at the start
        std::string humanClass;        // MAGE ...
        std::string opponentClass;
        std::string humanCards;        // EncodeCardList of the player's deck
        std::string opponentCards;
    };

    // One entry per copy -> "<card id>.<copies>,..." sorted by card id ("" for an empty list).
    std::string EncodeCardList(std::vector<std::string> const& cards);

    // Longest values of the text columns, in bytes (see data/sql/character/0004_hearthstone_match_history.sql).
    constexpr std::size_t kHistoryCharNameBytes = 24;
    constexpr std::size_t kHistoryNameBytes = 64;
    constexpr std::size_t kHistoryKeyBytes = 32;
    constexpr std::size_t kHistoryClassBytes = 16;
    constexpr std::size_t kHistoryCodeBytes = 12; // difficulty, result, end reason
    constexpr std::size_t kHistoryCardsBytes = 1024;

    struct MatchHistoryRow
    {
        uint32_t accountId = 0;
        uint32_t characterGuid = 0;
        std::string characterName;
        uint32_t matchId = 0;
        uint32_t opponentEntry = 0;
        std::string opponentName;
        std::string difficulty;        // normal, hard, boss
        uint32_t deckId = 0;
        std::string deckName;
        std::string deckClass;
        std::string opponentDeckKey;
        std::string opponentClass;
        std::string result;            // win, loss, draw, surrender, interrupted (unknown: never expected)
        std::string endReason;         // MatchEnd::why
        uint32_t turns = 0;
        int64_t startedAt = 0;         // unix seconds; endedAt - durationSec when the start is unknown
        int64_t endedAt = 0;
        uint32_t durationSec = 0;
        uint32_t seed = 0;
        std::string datasetId;
        std::string datasetHash;       // the first 16 hex characters of the content hash
        uint32_t rulesVersion = 0;     // Hs::Match::kRulesVersion
        uint32_t aiDelayMs = 0;
        std::string humanCards;
        std::string opponentCards;
        uint32_t currencyPaid = 0;
    };

    // The result word: win, loss, draw, interrupted (res "int") and surrender (a loss by why "concede");
    // "unknown" for a res the service never produces.
    std::string HistoryResultOf(std::string const& res, std::string const& why);

    // The value of one text column: cut at maxBytes on a UTF-8 character boundary, every 4-byte character
    // replaced by '?' (the character database connection is utf8mb3 for the other tables of the module) and
    // control characters replaced by a space.
    std::string ClipHistoryText(std::string const& text, std::size_t maxBytes);

    // One row as the GM command lists it (.hs match history).
    struct MatchHistoryListing
    {
        uint64_t id = 0;       // hearthstone_match_history.id
        std::string endedText; // the end time as text, "YYYY-MM-DD HH:MM"
        MatchHistoryRow row;   // only the listed columns are filled
    };

    // "#12 2026-10-02 14:03 Eqwe vs Innkeeper Gryshka [normal], deck "Mage 1" (MAGE) vs normal_mage: win (hero),
    // 8 turns, 5m12s, seed 123456".
    std::string FormatMatchHistoryLine(MatchHistoryListing const& entry);

    // "5m12s", "45s", "1h02m".
    std::string FormatHistoryDuration(uint32_t seconds);

    // The row of a match that just ended. endedAt: the current unix time in seconds. Never fails; a missing
    // value stays empty / 0.
    MatchHistoryRow BuildMatchHistoryRow(MatchEnd const& end, int64_t endedAt);
}

#endif
