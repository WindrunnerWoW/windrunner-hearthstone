/*
 * World NPC Ladder: the ladder rules, the roster assignment and the NPC rounds.
 *
 * PURE C++ (standard library, the headless game runner and the match engine, no core headers, no database). The server
 * glue loads a LadderState from its tables, calls these functions and saves what they report; nothing here keeps
 * hidden state, reads a clock or uses a global RNG. Every function either reads a LadderState (Plan*, order) or
 * changes exactly the state it is given (Apply*, UpdateRoster, Place*).
 *
 * Star ladder (the same for NPCs and the player): ranks 25 (bottom) to 1, then Legend (rank 0). Stars: 25..21 hold 2,
 * 20..16 hold 3, 15..10 hold 4, 9..1 hold 5. A win is +1 star; a win at full stars moves up with 1 star; a win at rank 1 with
 * full stars enters Legend. A loss is -1 star; a loss at 0 stars moves down with (that rank's max stars - 1);
 * rank 25 with 0 stars stays; a loss in Legend costs no star. A draw changes no stars. Streak bonus (PLAYER ONLY):
 * a win that makes 3 or more wins in a row, from a rank worse than 5 (6..25), gives one more star. A loss or a draw
 * resets the streak (choice: a draw is not a win, so it ends the run; it costs nothing else).
 *
 * Legend has rules.legendSize places (10). When more enabled entrants stand in Legend, the one with the lowest
 * hidden rating (ties: the higher id, the last in ladder order) goes to rank 1 with 4 stars, until legendSize
 * remain. The check runs after a whole NPC round is applied and after a single player result.
 *
 * Hidden rating: Elo with K 32, stored as integer centi-points (100000 = 1000.00). The change for A is
 * round-half-away-from-zero(3200 * (S - E)) centi-points with E = 1/(1+10^((Rb-Ra)/400)); B gets exactly the
 * negative, so the pair's total never changes. Every NPC and the player has one.
 *
 * Ladder order (the top list): rank ascending (Legend first), stars descending, hidden rating descending, id
 * ascending. In Legend all stars are 0, so Legend is ordered by hidden rating. Players and NPCs share one order.
 *
 * Rounds are split in three steps so the glue can plan on the world thread, play on a worker and apply on the world
 * thread:
 *   PlanRound   pure and cheap: pairings, bye, seeds                       (reads the state)
 *   PlayRound   plays the planned games, optionally on N threads           (reads nothing of the state)
 *   ApplyRound  applies the results in slot order, then the Legend check   (changes the state)
 * The same state and the same round number give the same plan, the same games and the same result, so a round lost
 * in a crash is replayed identically from the saved state.
 *
 * Seeds: round seed = Mix(ladder seed, round number); game seed = Mix(round seed, slot). Round number =
 * state.roundsPlayed + 1. Who starts a game: the game seed decides (firstSide -1 of SimulateGame; the coin toss
 * is unbiased, and the entrant listed first is only the engine's side 0, not the first player).
 *
 * What the glue has to store beyond the plan's column list: Entrant::byes and Entrant::lastOpponent (the pairing
 * needs them to be identical after a restart), winStreak, bestRank/bestStars, and the VsRecord rows.
 */

#ifndef MOD_HEARTHSTONE_HS_LADDER_RULES_H
#define MOD_HEARTHSTONE_HS_LADDER_RULES_H

#include "HsLadderSim.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Hs
{
namespace Ladder
{
    // ---- ids and constants ------------------------------------------------------------------------------

    // One id space for everything on the ladder: an NPC is its creature entry, a player entry is
    // kPlayerIdBase + the account id. NPC ids sort before player ids (the stable id of exact ties).
    using EntrantId = uint64_t;
    constexpr EntrantId kPlayerIdBase = 1ull << 32;
    inline EntrantId NpcId(uint32_t creatureEntry) { return creatureEntry; }
    inline EntrantId PlayerId(uint32_t accountId) { return kPlayerIdBase + accountId; }
    inline bool IsPlayerId(EntrantId id) { return id >= kPlayerIdBase; }

    constexpr int kLegend = 0;       // rank 0
    constexpr int kBottomRank = 25;
    constexpr int kTopRank = 1;
    constexpr int kPushedOutStars = 4; // a Legend member pushed out lands on rank 1 with 4 of its 5 stars
    constexpr int32_t kStartRating = 100000; // 1000.00 in centi-points
    constexpr int kEloK = 32;
    constexpr int kStreakBonusFrom = 3;   // wins in a row
    constexpr int kStreakBonusAbove = 5;  // the bonus star needs a rank number above this (6..25)

    // The numbers a server may want to change; the defaults are the owner decisions.
    struct LadderRules
    {
        int legendSize = 10;  // places in Legend
        int startRounds = 20; // rating-only rounds of the ladder start
    };

    enum class Result : uint8_t { Win, Draw, Loss }; // from one entrant's point of view
    char const* ResultCode(Result result);           // "win" "draw" "loss"

    enum class Faction : uint8_t { Alliance, Horde, Any };
    char const* FactionCode(Faction faction);                      // "alliance" "horde" "any"
    bool ParseFaction(std::string const& code, Faction& out);

    // ---- stars ---------------------------------------------------------------------------------------------

    int MaxStars(int rank); // 2 for 25..21, 3 for 20..16, 4 for 15..10, 5 for 9..1, 0 for Legend

    struct Stars
    {
        int rank = kBottomRank; // 15 .. 1, 0 = Legend
        int stars = 0;          // 0 .. MaxStars(rank)
    };
    bool operator==(Stars const& a, Stars const& b);

    Stars StarUp(Stars s);   // one star more (promotion at full stars, Legend at rank 1 full)
    Stars StarDown(Stars s); // one star less (demotion at 0 stars, floor at rank 15, nothing in Legend)
    // True when a is strictly higher on the ladder than b (lower rank number; same rank: more stars).
    bool BetterPosition(Stars a, Stars b);

    // ---- hidden rating ---------------------------------------------------------------------------------

    // The change of A's rating in centi-points; B's change is exactly the negative.
    int32_t EloDeltaA(int32_t ratingA, int32_t ratingB, Result resultOfA);

    // ---- state ---------------------------------------------------------------------------------------------

    // One ladder entry: an NPC or a player. Plain fields, one row each in storage.
    struct Entrant
    {
        EntrantId id = 0;            // NpcId(creature entry) or PlayerId(account)
        std::string name;            // display name (runtime only, refreshed from the finder list, not stored)
        // NPC identity, assigned once and for good (empty / defaults for a player).
        Faction faction = Faction::Any;
        std::string heroClass;       // MAGE ... WARRIOR
        std::string deckKey;         // library deck key
        Match::AiProfile profile = Match::AiProfile::Normal;
        bool enabled = true;         // false: left the roster; keeps its record, is out of rounds and the order
        uint32_t joinedRound = 0;    // state.roundsPlayed when it joined
        // Standing.
        int rank = kBottomRank;
        int stars = 0;
        int32_t rating = kStartRating;
        uint32_t games = 0, wins = 0, losses = 0, draws = 0;
        int winStreak = 0;           // player only (NPCs never use the streak bonus)
        int bestRank = kBottomRank;  // the best position it held: highest rank, then stars (Legend = 0)
        int bestStars = 0;
        // Pairing memory.
        uint32_t byes = 0;           // NPC rounds it sat out because of an odd count (excluded does not count)
        EntrantId lastOpponent = 0;  // its opponent in its last NPC-versus-NPC game (0 = none)

        bool IsPlayer() const { return IsPlayerId(id); }
        Stars Position() const { return Stars{rank, stars}; }
    };

    // A player's record against one NPC (hearthstone_ladder_player_vs).
    struct VsRecord
    {
        EntrantId player = 0;
        EntrantId npc = 0;
        uint32_t wins = 0, losses = 0, draws = 0; // the player's results
    };

    struct LadderState
    {
        uint32_t seed = 0;         // the stored ladder seed
        uint32_t roundsPlayed = 0; // rounds applied so far (the ladder start's rating-only rounds included)
        bool started = false;      // the NPCs have been placed on the star ladder
        std::vector<Entrant> entrants;
        std::vector<VsRecord> vs;

        Entrant* Find(EntrantId id);
        Entrant const* Find(EntrantId id) const;
        VsRecord const* FindVs(EntrantId player, EntrantId npc) const;
    };

    // FNV-1a 64 over every stored field in id order (a quick equality check for tests and for a replay check).
    uint64_t StateHash(LadderState const& state);

    // Ladder order of Entrant a before b (see the file comment).
    bool LadderLess(Entrant const& a, Entrant const& b);
    // The enabled entrants (NPCs and players) in ladder order. Pointers stay valid until the state changes.
    std::vector<Entrant const*> LadderOrder(LadderState const& state);

    // The player entry of an account (created at rank 15, 0 stars, rating 1000 on first call; idempotent).
    Entrant& EnsurePlayer(LadderState& state, uint32_t accountId, std::string const& name);

    // ---- roster -----------------------------------------------------------------------------------------

    enum class DeckRating : uint8_t { Easy, Hard };
    char const* DeckRatingCode(DeckRating rating); // "easy" "hard"

    // One name of the finder list (enabled rows of hearthstone_finder_npc with the name from creature_template).
    struct FinderName
    {
        uint32_t entry = 0;
        Faction faction = Faction::Any;
        std::string name;
    };

    // One library deck (hearthstone_deck_template, kind library; finder normal = Easy, hard = Hard).
    struct LibraryDeck
    {
        std::string key;
        std::string heroClass;
        DeckRating rating = DeckRating::Easy;
        std::vector<std::string> cards; // card ids, one entry per copy
    };

    struct RosterChange
    {
        std::vector<EntrantId> added;      // new NPCs (entry order)
        std::vector<EntrantId> disabled;   // were enabled, no longer in the finder list
        std::vector<EntrantId> enabled;    // were disabled, back in the finder list (old record kept)
        std::vector<EntrantId> pushedOut;  // Legend members pushed out because a re-enabled NPC filled Legend
        std::vector<uint32_t> unassigned;  // finder entries that got no assignment (the library has no deck)
    };

    // The nine classes, in a fixed order.
    extern char const* const kClasses[9];
    // The four (AI profile, deck rating) combinations in rotation order: (Normal, easy), (Hard, hard),
    // (Normal, hard), (Hard, easy).
    struct MixSlot
    {
        Match::AiProfile profile;
        DeckRating rating;
    };
    extern MixSlot const kMix[4];

    // Brings the NPCs of the state in line with the finder list (any order; sorted by entry internally):
    //  - a new entry becomes an NPC at rank 15, 0 stars, rating 1000, enabled, joinedRound = roundsPlayed, with
    //    its faction from the row and, once and for good: a class (the class with the fewest enabled NPCs, a tie
    //    picked by a hash of the ladder seed and the entry), the mix slot least used in that class (rotation order
    //    on a tie; slots whose deck rating the class lacks are skipped) and its least used deck of that rating
    //    (key order on a tie);
    //  - an NPC missing from the list is disabled (record kept), one that returns is enabled again;
    //  - existing assignments never change; names are refreshed.
    // Legend is re-checked after re-enabling. Entries for which the library has no deck are reported, not added.
    RosterChange UpdateRoster(LadderState& state, std::vector<FinderName> const& finder,
                              std::vector<LibraryDeck> const& library, LadderRules const& rules = LadderRules());

    // ---- ladder start ------------------------------------------------------------------------------------

    // Places the enabled NPCs on the star ladder by hidden rating (ties: id): the first rules.legendSize in
    // Legend, the rest spread over ranks 1..15 (best at rank 1) with as equal counts per rank as possible, 0 stars
    // (m NPCs: the i-th, best first, gets rank 1 + floor(i * 15 / m)). Fewer NPCs than places: all in Legend.
    // Players are not touched. Sets state.started.
    void PlaceNpcs(LadderState& state, LadderRules const& rules = LadderRules());

    // ---- rounds --------------------------------------------------------------------------------------------

    enum class RoundKind : uint8_t
    {
        RatingOnly, // ladder start: pairs by hidden rating, only hidden ratings and counters change
        Ladder      // normal: pairs by ladder order, stars and hidden ratings change
    };

    struct RoundOptions
    {
        RoundKind kind = RoundKind::Ladder;
        EntrantId excludedNpc = 0; // sits out (the player's last opponent); not a bye
    };

    // One planned game; entrant a is the engine's side 0.
    struct GamePlan
    {
        uint32_t slot = 0;
        EntrantId a = 0, b = 0;
        uint32_t seed = 0;
        int firstSide = -1; // -1: the game seed decides
    };

    struct RoundPlan
    {
        uint32_t round = 0;       // state.roundsPlayed + 1
        RoundKind kind = RoundKind::Ladder;
        uint32_t seed = 0;        // the round seed
        EntrantId excluded = 0;   // as requested
        EntrantId bye = 0;        // the NPC that sits out because of an odd count (0 = none)
        std::vector<GamePlan> games;
    };

    // Pairings, bye and seeds. Rating-only: sorted by hidden rating (ties by a hash of the round seed and the id,
    // so round 1 is a seeded shuffle), neighbours play. Ladder: sorted by ladder order, neighbours play. In both,
    // an NPC whose last opponent is its neighbour is swapped with the nearest entrant that avoids the repeat (for
    // both new pairs); an odd count gives the bye to the NPC with the fewest byes (ties: the smallest hash of the
    // round seed and the id), after the excluded NPC was removed. Only enabled NPCs take part.
    RoundPlan PlanRound(LadderState const& state, RoundOptions const& options = RoundOptions());

    // The outcome of one game, as storage keeps it (hearthstone_ladder_game).
    struct GameResult
    {
        GamePlan plan;
        SimOutcome outcome = SimOutcome::Failed;
        int firstSide = -1; // who really started (from SimResult)
        int turns = 0;
        std::string error;  // Failed
    };
    GameResult ToGameResult(GamePlan const& plan, SimResult const& sim);

    // Plays one planned game. Must be safe to call from several threads at once.
    using GameFn = std::function<SimResult(GamePlan const&)>;

    // The SimEntrants of every NPC whose library deck is known (an NPC without one is missing: its games fail).
    using SimRoster = std::map<EntrantId, SimEntrant>;
    SimRoster BuildSimRoster(LadderState const& state, std::vector<LibraryDeck> const& library);
    // The real game function: SimulateGame over the roster. db and roster must outlive the function.
    GameFn MakeSimGameFn(Match::CardDb const& db, SimRoster const& roster, SimOptions const& options = SimOptions());

    // Plays all games of the plan (threads <= 1: on the calling thread, else on that many workers); results in slot
    // order and identical to the serial run. A game function that throws makes a Failed result.
    std::vector<GameResult> PlayRound(RoundPlan const& plan, GameFn const& game, int threads = 1);

    struct RoundReport
    {
        bool ok = false;
        std::string error;                 // when !ok: nothing was applied
        uint32_t round = 0;
        int played = 0;                    // games applied
        std::vector<GameResult> failures;  // Failed games: nothing changed for either NPC
        std::vector<EntrantId> touched;    // entrants whose stored row changed (sorted)
        std::vector<EntrantId> pushedOut;  // pushed out of Legend by the check
    };

    // Applies the results in slot order: counters, hidden rating and (Ladder rounds) stars of both NPCs, the last
    // opponents, the bye; then the Legend check (Ladder rounds); then best positions. plan.round must be
    // state.roundsPlayed + 1 and results must belong to the plan, else nothing is applied. roundsPlayed advances
    // by one even when every game failed (a failed game is not rerolled).
    RoundReport ApplyRound(LadderState& state, RoundPlan const& plan, std::vector<GameResult> const& results,
                           LadderRules const& rules = LadderRules());

    // Plan + play + apply in one call (tests, tools, simple glue).
    RoundReport RunRound(LadderState& state, GameFn const& game, int threads = 1,
                         RoundOptions const& options = RoundOptions(), LadderRules const& rules = LadderRules());

    // The ladder start: plays rating-only rounds until state.roundsPlayed == rules.startRounds (so an interrupted
    // start continues where it stopped), then PlaceNpcs. onRound is called after each applied round with the
    // report (the glue saves there). Does nothing when state.started. Returns the rounds played by this call.
    int RunLadderStart(LadderState& state, GameFn const& game, int threads = 1,
                       LadderRules const& rules = LadderRules(),
                       std::function<void(LadderState const&, RoundReport const&)> const& onRound = nullptr);

    // Pushes the lowest Legend members to rank 1 with 4 stars until rules.legendSize remain; returns their ids.
    std::vector<EntrantId> EnforceLegendLimit(LadderState& state, LadderRules const& rules = LadderRules());

    // ---- player result ---------------------------------------------------------------------------------

    struct PlayerResultReport
    {
        bool ok = false;
        std::string error;            // unknown_player, unknown_npc, same_entrant
        Stars before, after;          // the player's position
        bool streakBonus = false;     // the win earned the extra star
        int winStreak = 0;            // after this result
        int32_t ratingDelta = 0;      // the player's change in centi-points (the NPC's is the negative)
        Stars npcBefore, npcAfter;
        bool newBest = false;         // the player's best position improved
        std::vector<EntrantId> pushedOut; // pushed out of Legend by the check
    };

    // Applies a player's win, loss or draw against an NPC (a disabled NPC is accepted): the player's stars with
    // the streak bonus, the NPC's stars without it, both hidden ratings (zero-sum), the counters, the player's
    // record against the NPC, the Legend check and the best positions. The following NPC round is the glue's
    // (RoundOptions::excludedNpc = npc).
    PlayerResultReport ApplyPlayerResult(LadderState& state, EntrantId player, EntrantId npc, Result playerResult,
                                         LadderRules const& rules = LadderRules());

    // ---- seeds (public so tests and glue can reproduce a plan) -----------------------------------------

    uint32_t Mix(uint32_t a, uint32_t b);
    uint32_t RoundSeed(uint32_t ladderSeed, uint32_t round);
    uint32_t GameSeed(uint32_t roundSeed, uint32_t slot);
}
}

#endif
