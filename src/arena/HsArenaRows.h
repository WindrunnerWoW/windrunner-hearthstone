/*
 * Draft arena rows: the pure part of the arena database storage, so that it can be unit-tested
 * headlessly. The SQL itself is in src/HsArenaStore.cpp, the run/duel lifecycle in src/HsArenaGlue.cpp.
 *
 * What lives here:
 *   - the text forms of the id lists (offers, the opponent's drafted deck) and their strict decoders;
 *   - ArenaOpponent, the row of one duel (hearthstone_arena_match) and the check that it can still be played;
 *   - the match start parameters of an arena duel, from the run and its stored opponent;
 *   - the decoder of the stored replay log (hearthstone_arena_action rows, Hs::EncodeAction text);
 *   - the mapping of a finished match's res= word to the run's ArenaDuelResult.
 *
 * PURE C++ (standard library, the arena service and the match engine headers, no core headers).
 */

#ifndef MOD_HEARTHSTONE_HS_ARENA_ROWS_H
#define MOD_HEARTHSTONE_HS_ARENA_ROWS_H

#include "HsArenaRules.h"
#include "HsArenaService.h"
#include "../match/HsMatchReplay.h"
#include "../match/HsMatchService.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Hs
{
    // Longest values of the text columns, in bytes (data/sql/character/0010_hearthstone_arena.sql).
    constexpr std::size_t kArenaOfferBytes = 112;
    constexpr std::size_t kArenaOpponentNameBytes = 64;
    constexpr std::size_t kArenaOpponentDeckBytes = 1100;
    constexpr std::size_t kArenaActionBytes = 160;
    // Every arena duel is played at this difficulty (Match::AiProfile::Hard), at every win count.
    constexpr char const* kArenaDifficulty = "hard";

    // "a,b,c": the ids joined by commas ("" for none).
    std::string EncodeArenaIds(std::vector<std::string> const& ids);
    // The inverse. Strict: every id is [A-Za-z0-9_]{1,32}, no spaces, at most maxCount ids; "" is an empty list.
    // False leaves out untouched.
    bool DecodeArenaIds(std::string const& text, std::size_t maxCount, std::vector<std::string>& out);

    // One duel of a run (hearthstone_arena_match): the AI opponent, persisted before the duel starts, and what a
    // paused duel needs besides its action log.
    struct ArenaOpponent
    {
        uint32_t runId = 0;
        uint32_t matchNo = 0;
        std::string deckClass;          // MAGE ...
        std::string name;               // the NPC name the opponent plays under
        std::vector<std::string> deck;  // the 30 drafted ids in drafted order (the shuffle depends on the order)
        uint32_t seed = 0;
        uint32_t timerMs = 0;           // the remaining turn timer of a paused duel (0 = none)
        int rulesVersion = 0;           // what the log was recorded with
        std::string datasetHash;
        int64_t startedAt = 0;          // unix seconds
        std::string result;             // "" until decided, then win draw loss
    };
    // The opponent can be played: one of the nine classes, a name, a legal arena deck of that class. questOwned: the
    // run's quest snapshot (the same one its AI draft used); nullptr: no quest card.
    bool ArenaOpponentOk(ArenaOpponent const& opponent, CardCatalog const& catalog, Match::CardDb const& db,
                         ArenaQuestSet const* questOwned = nullptr);

    // The start parameters of a run's duel for the character guid playing it: the human side is the run's class and
    // drafted deck (in pick order), the opponent the stored one; arena = true, Hard AI, difficulty "hard".
    // hostEntry: the creature entry of the Hearthstone Master (history row), accountId from the run.
    MatchStartParams MakeArenaDuelParams(ArenaRun const& run, ArenaOpponent const& opponent, uint32_t guid,
                                         std::string const& humanName, uint32_t hostEntry);
    // The duel a paused run restores: the parameters, the decoded log and the versions and timer of the row.
    SuspendedDuel MakeSuspendedDuel(MatchStartParams const& params, std::vector<LoggedAction> const& log,
                                    ArenaOpponent const& opponent);

    // The stored log rows (in idx order) decoded. False with the index of the first row that does not decode and the
    // decoder's reason.
    bool DecodeArenaLog(std::vector<std::string> const& rows, std::vector<LoggedAction>& out, std::size_t& failedAt,
                        std::string& error);

    // The res= word of a finished match (MatchEnd::res) as the run's result: win, draw and loss. False for anything
    // else (int: an interrupted match is no result).
    bool ArenaDuelResultOf(std::string const& res, ArenaDuelResult& out);
    char const* ArenaDuelResultCode(ArenaDuelResult result); // "win", "draw", "loss"
}

#endif
