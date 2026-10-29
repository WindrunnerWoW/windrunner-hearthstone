/*
 * Match service: the running and finished matches of the characters
 * in the world, one per character guid. The human is side 0 and the AI side 1;
 * both go through the same Match::Apply path. The service paces the AI
 * (Tick), keeps the per-character "events already sent" mark, ends matches from
 * outside (Abort with a reason code) and builds the GAME payloads.
 *
 * PURE C++ (standard library, the match engine and the wire codec), tested
 * headless in tests/protocol_tests.cpp. The game glue (HearthstoneScripts.cpp,
 * HsMatchGlue.cpp) owns everything that touches the core: gossip, aura,
 * packet filter, pushes.
 *
 * Threading: every public method takes the service mutex. The mutex is a LEAF
 * lock: the service never calls another service, the logger or the game while
 * holding it, so callers may hold their own locks (the session registry mutex
 * of the glue) when they call in. Methods return what the caller has to do
 * (payloads to push, matches that ended) and never perform it.
 *
 * AI pacing: Tick(nowMs) ends a human turn whose Nozdormu deadline passed (below) and applies at most one AI
 * action per match, only when
 * the AI is the active side (or has a pending pick) and the human has
 * no pending pick, and its delay (AiDelayMs) has passed since the last action
 * of either side. The AI mulligans when the match is created. Every AI decision
 * goes through Match::ChooseAiAction(match, 1, db, actions, profile, aiSeed) with
 * aiSeed = AiSeedFor(match seed, seq): deterministic.
 *
 * Turn timer (Nozdormu EX1_560 "Players only have 15 seconds to take their turns"): while
 * Match::TurnTimeLimitSec() is 15 the active side's turn has a deadline = 15 s after the later of the
 * turn's start and the moment the limit became active during this turn (a Nozdormu played mid-turn gives
 * the rest of that turn a fresh 15 s; the limit going away mid-turn removes the deadline, and a later
 * Nozdormu starts 15 s again). Only the human's turns are timed: the AI's turn is paced by AiDelayMs per
 * action (a limit would cut it short) and needs only milliseconds to think. Tick(nowMs) enforces it: when the
 * deadline has passed and the match is in the main phase, a pending pick of the human is answered with its
 * FIRST option and then the turn ends as if the human had sent End. The clock moves only at Start / Act / Tick / Get, so
 * the deadline is relative to the newest nowMs the service saw (deterministic with an injected clock).
 * GAME carries tmr= (seconds left, rounded up, at least 1 while the timer runs; 0 = no timer).
 *
 * Events: every GAME payload (reply or push) carries the events since the last
 * GAME payload built for this character and moves the mark to the newest event;
 * GAME_GET moves the mark without sending events (a resync).
 *
 * Durable duels: a match started with
 * MatchStartParams::arena keeps the log of every action applied to it (the AI's opening mulligan, the human's
 * actions, the AI's actions and the ones the turn timer forces; HsMatchReplay.h). After each successful Apply the
 * outcome of Start / Act / Tick lists the new entries (logged, from index loggedFirst); the glue stores them
 * BEFORE it sends the payload, and when the store fails it calls Suspend (keepActions = what was stored) instead of
 * showing progress that is not saved. Suspend(guid, out) frees the entry and fills a SuspendedDuel: the start
 * parameters, the log, the remaining timer and the rules version and card dataset hash the duel was played with.
 * Restore(duel, nowMs) is Match::Create plus Apply of the log (the same match, event for event), recomputes the AI's
 * per-turn counters from the log, restarts the timer with the saved remainder and moves the events mark to the newest
 * event, so the next GAME payload is a clean resync. It refuses a duel saved with another rules version or dataset
 * ("rules", "dataset"). Ordinary matches keep no log and behave exactly as before.
 */

#ifndef MOD_HEARTHSTONE_HS_MATCH_SERVICE_H
#define MOD_HEARTHSTONE_HS_MATCH_SERVICE_H

#include "HsMatch.h"
#include "HsMatchAi.h"
#include "HsMatchHistory.h"
#include "HsMatchReplay.h"
#include "HsMatchWire.h"

#include "../decks/HsDeckService.h"
#include "../decks/HsDecks.h"

#include <atomic>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Hs
{
    // AI pacing bounds (Hearthstone.Match.AiActionDelayMs).
    constexpr uint32_t kMinAiDelayMs = 200;
    constexpr uint32_t kMaxAiDelayMs = 10000;
    constexpr uint32_t kDefaultAiDelayMs = 1500;

    // Why a running match is ended from outside. why= of the GAME payload.
    enum class Interrupt { Stuck, Teleport, Death, Npc, Gm, Error };
    char const* InterruptCode(Interrupt why); // stuck teleport death npc gm error

    // A match that just ended, for logging and the cleanup of the glue.
    struct MatchEnd
    {
        uint32_t guid = 0;       // the character
        uint32_t matchId = 0;
        int turn = 0;
        int ownTurns = 0;
        std::array<Match::SideTally, 2> tallies;
        std::string res;         // win loss draw int, from the human's point of view
        std::string why;         // hero concede turns stuck teleport death npc gm error (logout: only from Drop)
        std::string humanName;
        std::string opponentName;
        std::string difficulty;  // normal, hard or boss
        MatchMeta meta;          // what the match history row needs (HsMatchHistory.h)
        // The match was a Draft Arena duel (MatchStartParams::arena) of this run.
        bool arena = false;
        uint64_t arenaRunId = 0;
    };

    struct MatchStartParams
    {
        uint32_t guid = 0; // the character (key of the match)
        std::string humanName;
        std::string humanClass; // the deck class: MAGE ...
        std::vector<std::string> humanDeck; // card ids, one entry per copy
        std::string opponentName;
        std::string opponentClass;
        std::vector<std::string> opponentDeck;
        uint32_t seed = 0;
        // The difficulty shown to the player (dif= of GAME: normal, hard or boss) and the AI
        // profile that plays the opponent (Normal for normal, Hard for hard and boss).
        std::string difficulty = "normal";
        Match::AiProfile profile = Match::AiProfile::Normal;
        // Encounter overrides of the opponent (SideSetup of side 1; 0 / empty = the normal rules).
        int opponentHeroHealth = 0;
        int opponentHeroArmor = 0;
        std::string opponentHeroCardId;
        std::string opponentHeroPowerId;
        // Boss encounters (HsEncounters.h): the starting board / weapon / extra hand cards / no-draw rule of
        // each side and the forced first side (0 = the human, 1 = the opponent, -1 = the seeded coin toss). All at their
        // defaults for an ordinary match.
        Match::SideStart humanStart;
        Match::SideStart opponentStart;
        int firstSide = -1;
        // The identity of the match for its history row (account, creature entry, deck id and name,
        // opponent deck key, start time); the service adds the seed, the classes, the card lists and the
        // AI delay.
        MatchMeta meta;
        // A Draft Arena duel. The service keeps the action log (see the header comment), the match can
        // be suspended and restored, and arenaRunId is the run it belongs to (opaque to the service).
        bool arena = false;
        uint64_t arenaRunId = 0;
        // The durable duel is a ranked ladder duel (the GAME payload says dur=1, so the addon pauses it when its
        // window is closed). Always together with arena = true.
        bool ladder = false;
    };

    // A duel stored by its start parameters and the log of applied actions.
    struct SuspendedDuel
    {
        MatchStartParams params;           // arena = true; params.guid is the character that plays it
        std::vector<LoggedAction> log;     // every applied action in order (the AI's mulligan is the first)
        // Milliseconds left of the Nozdormu turn deadline when it was suspended (at least 1 when a deadline was
        // running, even if it had passed); 0 = no deadline running (a fresh one starts if the limit applies).
        uint32_t timerMs = 0;
        // What the log was played with; Restore refuses a different rules version or card dataset.
        int rulesVersion = 0;              // Match::kRulesVersion
        std::string datasetHash;           // kDatasetHash (HsDataset.h)
        bool over = false;                 // the match had ended (only a Suspend of a finished entry)
    };

    struct MatchStartResult
    {
        bool ok = false;
        std::string error;   // when !ok: "running", "engine" or the engine's own text
        uint32_t matchId = 0;
        std::string payload; // the first GAME payload
        // Arena only: the log entries the start applied (the AI's mulligan), from index 0.
        uint64_t arenaRunId = 0;
        std::vector<LoggedAction> logged;
    };

    struct MatchRestoreResult
    {
        bool ok = false;
        // when !ok: "running" (the character has a running match), "engine" (no card database), "arena" (the duel
        // is not an arena duel), "rules" (rules version differs), "dataset" (card dataset differs), "log" (an
        // action of the log was refused or the log runs past the end of the match; failedAt is its index) or the
        // engine's own text (the setup does not create a match).
        std::string error;
        uint32_t failedAt = 0;
        uint32_t matchId = 0;
        std::string payload; // the resync GAME payload (no events)
        bool ended = false; // replay reached the authoritative end before a crash
        MatchEnd end;
    };

    struct GameActOutcome
    {
        bool ok = false;
        std::string why;     // !ok: an engine code, stale, match or over
        std::string payload; // ok: the GAME payload
        bool ended = false;  // the match ended with this action
        MatchEnd end;
        std::vector<Match::Event> events; // public events of this action, for world encounter narration
        // Arena only: the log entries this action added, from index loggedFirst. Store them before
        // the payload is sent.
        uint64_t arenaRunId = 0;
        uint32_t loggedFirst = 0;
        std::vector<LoggedAction> logged;
    };

    struct GameLeaveOutcome
    {
        bool ok = false;
        std::string why;     // !ok: active
        std::string payload; // ok: GAME ph=none
    };

    struct MatchAbortOutcome
    {
        bool ended = false;  // false: there was no running match
        std::string payload; // the GAME payload to push
        MatchEnd end;
    };

    // One AI action was applied: the human's GAME push.
    struct MatchPush
    {
        uint32_t guid = 0;
        uint32_t matchId = 0;
        std::string payload;
        bool ended = false;
        MatchEnd end;
        std::string note;    // diagnostics for the log (an AI action the engine refused)
        std::vector<Match::Event> events;
        // Arena only: the run of an arena match and the log entries this tick added (the AI's action,
        // or the timer's pick answer and EndTurn), from index loggedFirst. Store them before the payload is sent.
        uint64_t arenaRunId = 0;
        uint32_t loggedFirst = 0;
        std::vector<LoggedAction> logged;
    };

    struct MatchInfo
    {
        uint32_t guid = 0;
        uint32_t matchId = 0;
        std::string humanName;
        std::string opponentName;
        bool running = false;
        int turn = 0;
        int humanHealth = 0;
        int opponentHealth = 0;
        uint32_t seq = 0;
        uint32_t seed = 0;
        std::string res;     // - while running
        std::string why;
        std::string difficulty; // normal, hard or boss
    };

    class MatchService
    {
    public:
        // firstMatchId: the first match id handed out (the glue starts at a time based value
        // so ids do not repeat across restarts).
        explicit MatchService(uint32_t firstMatchId = 1);

        // The card definitions of the engine; Start fails with "engine" until it is set.
        // The service keeps the database alive as long as a match uses it.
        void SetCardDb(std::shared_ptr<Match::CardDb const> db);
        std::shared_ptr<Match::CardDb const> CardDb() const;

        // Hearthstone.Match.AiActionDelayMs, clamped to kMinAiDelayMs..kMaxAiDelayMs.
        void SetAiDelayMs(uint32_t ms);
        uint32_t AiDelayMs() const;

        // Creates a match (the AI mulligans at once). A finished match of the character is
        // replaced; a running one refuses with error "running". Nothing is stored on failure.
        MatchStartResult Start(MatchStartParams const& params, uint64_t nowMs);

        // GAME_GET: the current state without events (the mark moves to the newest event);
        // "ph=none" without a match.
        // NowMs (the service clock; 0 = the newest clock value the service has seen) is the base of
        // the tmr= countdown.
        std::string Get(uint32_t guid, uint64_t nowMs = 0);

        // GAME_ACT of the human. Refusals: match (no match, or its id differs), over, stale
        // (seq differs; not for a concede), or the engine's code. Nothing changes on a refusal.
        GameActOutcome Act(uint32_t guid, GameActRequest const& request, uint64_t nowMs);

        // GAME_LEAVE: drops a finished match with this id (and answers ph=none also when
        // there is nothing to leave); refuses with "active" while that match is running.
        GameLeaveOutcome Leave(uint32_t guid, uint32_t matchId);

        // Ends a running match from outside (no winner). No effect on a finished or missing match.
        MatchAbortOutcome Abort(uint32_t guid, Interrupt why);

        // Logout: drops the character's match (running or finished) without a push.
        // Returns true if a running match was dropped (end is filled in when not null).
        bool Drop(uint32_t guid, MatchEnd* end = nullptr);

        // True for the character's arena match (running or finished).
        bool IsArena(uint32_t guid) const;
        // Takes the character's arena match (running or finished) out of the service into out; false for no match
        // or an ordinary one (nothing changes then). nowMs (0 = the newest clock value the service has seen) is the
        // base of the remaining timer. keepActions (default: all) cuts the log to its first keepActions entries and
        // restarts the timer: for a store that failed, so that what is restored is what the player was shown.
        bool Suspend(uint32_t guid, SuspendedDuel& out, uint64_t nowMs = 0,
                     std::size_t keepActions = static_cast<std::size_t>(-1));
        // Rebuilds a suspended duel under duel.params.guid by replaying its log (Match::Create + Apply). The match
        // gets a new match id. A finished match of the character is replaced, a running one refuses ("running").
        // Nothing is stored on failure.
        MatchRestoreResult Restore(SuspendedDuel const& duel, uint64_t nowMs);

        // One AI action per due match. Returns the human's pushes.
        std::vector<MatchPush> Tick(uint64_t nowMs);

        bool IsRunning(uint32_t guid) const;
        bool Has(uint32_t guid) const;
        // Running matches; lock free, for the hot packet path.
        std::size_t RunningCount() const { return m_running.load(std::memory_order_relaxed); }
        std::size_t Count() const;
        // AI actions the engine refused so far (should stay 0); a diagnostic counter.
        uint32_t AiRefusals() const { return m_aiRefusals.load(std::memory_order_relaxed); }

        std::vector<MatchInfo> List() const;
        bool Info(uint32_t guid, MatchInfo& out) const;

        // Tests only (single threaded): the engine match of the character, for state surgery with
        // Match::TestAccess; nullptr without a match. The service does not notice changes made this way until
        // its next Act / Tick.
        Match::Match* MatchForTests(uint32_t guid);
        // Tests only: the AI's per-turn counters of the character's match (the Match::Turn() of its newest
        // action and the actions it took in that turn); false without a match.
        bool AiCountersForTests(uint32_t guid, int& turn, int& actions) const;

    private:
        struct Entry
        {
            uint32_t matchId = 0;
            std::shared_ptr<Match::CardDb const> db;
            std::unique_ptr<Match::Match> match;
            std::string humanName;
            std::string opponentName;
            std::string difficulty = "normal";
            MatchMeta meta;
            Match::AiProfile profile = Match::AiProfile::Normal;
            uint32_t sentEventSeq = 0;
            uint64_t nextAiMs = 0;
            int aiTurn = -1;
            int aiActions = 0;
            std::string abortWhy;
            // Arena only: the start parameters and the log of applied actions
            bool arena = false;
            std::shared_ptr<MatchStartParams const> params;
            std::vector<LoggedAction> log;
            // Nozdormu turn timer, see UpdateTimerLocked
            int timerTurn = -1;        // the Match::Turn() the deadline belongs to
            uint64_t deadlineMs = 0;   // 0 = no timer running
            uint64_t lastMs = 0;       // the newest clock value the service saw for this match
        };

        // All require m_mutex.
        void RecountLocked();
        std::string PayloadLocked(Entry& e, uint32_t sinceSeq, bool markNewest,
                                  std::vector<Match::Event>* events = nullptr);
        void UpdateTimerLocked(Entry& e, uint64_t nowMs);
        bool ExpireTurnLocked(Entry& e, uint64_t nowMs, std::string& note); // the deadline passed: pick, then End
        // Apply and, for an arena match, log the action when the engine accepted it.
        static Match::ActionError ApplyLocked(Entry& e, int side, Match::Action const& action);
        // The log entries from index `from` on.
        static std::vector<LoggedAction> LoggedSince(Entry const& e, std::size_t from);
        static uint32_t RemainingTimerMs(Entry const& e, uint64_t nowMs);
        Entry MakeEntryLocked(MatchStartParams const& params, std::unique_ptr<Match::Match> match, uint64_t nowMs);
        MatchEnd EndOfLocked(uint32_t guid, Entry const& e) const;
        static MatchInfo InfoOf(uint32_t guid, Entry const& e);

        mutable std::mutex m_mutex;
        std::shared_ptr<Match::CardDb const> m_db;
        uint32_t m_nextMatchId;
        uint32_t m_aiDelayMs = kDefaultAiDelayMs;
        std::unordered_map<uint32_t, Entry> m_matches;
        std::atomic<std::size_t> m_running{0};
        std::atomic<uint32_t> m_aiRefusals{0};
    };

    // ---- helpers of the glue (pure, tested) ----------------------------------

    // The seed of one AI decision (Match::ChooseAiAction): deterministic from the match seed and the
    // match's seq, so the same match state always gets the same decision.
    uint32_t AiSeedFor(uint32_t matchSeed, uint32_t seq);

    // Hearthstone.Match.Opponents: "<creature entry>:<class>,..." with any of the nine classes
    // (spaces allowed around the parts, case does not matter). Bad entries are skipped and described
    // in errors; a repeated entry keeps the first. The opponent plays normal_<class> or
    // hard_<class> of the deck library (see HsOpponents.h).
    struct MatchOpponent
    {
        uint32_t entry = 0;
        std::string deckClass; // MAGE ... WARRIOR
    };
    void ParseMatchOpponents(std::string const& text, std::vector<MatchOpponent>& out, std::vector<std::string>& errors);

    // One entry per copy, sorted by card id; false if the list is empty or longer than
    // Match::kMaxDeckCards.
    bool ExpandDeck(DeckCards const& cards, std::vector<std::string>& out);

    // Why a deck cannot be chosen at an opponent, for the player: the reasons of a CheckForMatch
    // that is not Ready(), joined with "; " ("" when it is ready). deckClass is the deck's class
    // (MAGE ...), cardName the name of check.unsupportedCard (its id when empty).
    std::string DeckNotReadyText(DeckMatchCheck const& check, std::string const& deckClass, std::string const& cardName);
}

#endif
