/*
 * mod-hearthstone: the core-facing glue of the World NPC Ladder: startup, the background worker
 * that plays the NPC rounds, the player-result entry point and the GM commands. The rules, the roster assignment
 * and the rounds are pure C++ in src/ladder; the database is HsLadderStore.*; this file connects them to the core.
 *
 * Threading. The WORLD thread owns the LadderState: startup, the world update, the GM commands and the player result
 * run there (one mutex in HsLadderGlue.cpp serializes them, so a later caller on a map thread is safe too). One
 * background worker thread plays whole rounds: a job is a RoundPlan plus shared_ptr snapshots of everything the
 * games read (the card database, the NPC decks), the finished round comes back through a one-slot hand-off. The
 * worker never touches the state, the world, a player or the database. The world thread applies a finished round to
 * the state, saves it in one transaction and only then plans the next one, so the state never changes under a
 * running round: while a job runs or work is queued, a player result is queued too and applied in order.
 *
 * Crash safety. The ladder start is Hearthstone.Ladder.StartRounds rating-only rounds, one at a time, each saved in
 * its own transaction, then the placement on the star ladder. A server stop (or crash) drops the round that was being
 * played: it is planned again from the saved state on the next start and plays identically (same seeds). If a save
 * fails, the in-memory state is rolled back to the one before the round and the round is planned again after a pause.
 *
 * Disabled (Hearthstone.Ladder.Enable = 0): nothing is loaded, no thread exists, the commands say so.
 */

#ifndef MOD_HEARTHSTONE_HS_LADDER_GLUE_H
#define MOD_HEARTHSTONE_HS_LADDER_GLUE_H

#include "ladder/HsLadderOffers.h"
#include "ladder/HsLadderRules.h"
#include "ladder/HsLadderWorld.h"

#include <cstdint>
#include <string>
#include <vector>

class ChatHandler;

// OnStartup, after the match, economy, trader and arena startups: reads the card data, the library decks, the finder
// names and the stored ladder, updates the roster, saves what changed and begins the ladder start when it has not
// finished. Does nothing when the ladder is disabled.
void HsLadderStartup();
// Every world update: takes a finished round from the worker, applies and saves it, plans the next work.
void HsLadderUpdate(uint32_t diff);
// OnShutdown: stops the worker (an unfinished round is dropped and replays identically at the next start).
void HsLadderShutdown();

// The ladder is enabled by the config, and loaded and usable.
bool HsLadderEnabled();
bool HsLadderReady();

// The answer of HsLadderPlayerResult.
struct HsLadderResultReply
{
    bool ok = false;
    std::string why;       // refusal code when !ok: disabled, unavailable, updating, unknown_npc, full, storage, stalled, ...
    bool queued = false;   // ok, but the ladder is busy: the result applies later, in order (nothing else is filled in)
    Hs::Ladder::PlayerResultReport report;
    Hs::Ladder::Entrant playerBefore, playerAfter; // the entries as they were / are (when applied now)
    Hs::Ladder::Entrant npcBefore, npcAfter;
    std::vector<std::string> milestones;           // milestones this result reached for the first time (text: Hs::Ladder::MilestoneText)
};

// A player's result against an NPC (used by the GM command and by ranked duels): ensures the account's
// entry (name: the character's name), applies the result, saves both entrants and the record in one transaction and
// queues one normal NPC round in which that NPC sits out. When the ladder is busy the result itself is queued.
HsLadderResultReply HsLadderPlayerResult(uint32_t accountId, std::string const& playerName, uint32_t npcEntry,
                                         Hs::Ladder::Result result);
// The text of a refusal code.
std::string HsLadderWhyText(std::string const& why);

// ---- the chairs (the window and the walk-up are HsLadderTable.*) -------------------------------------------------
// Everything below takes the ladder mutex itself; none of it may be called with that mutex held.

// "" when the ladder chairs can be used right now, else the reason, written for the player (the ladder is off or
// unavailable, the chair tables or the offer tables are not set up, the ladder start is still running, ...).
std::string HsLadderChairWhy();
// The ladder table whose player's chair is that gameobject guid / that table guid / whose city contains `city` (case
// ignored; false unless exactly one matches). False: not a ladder chair (or no world data).
bool HsLadderTableByChair(uint32_t chairGuid, Hs::Ladder::LadderTable& out);
bool HsLadderTableByGuid(uint32_t tableGuid, Hs::Ladder::LadderTable& out);
bool HsLadderTableByCity(std::string const& city, Hs::Ladder::LadderTable& out);
std::vector<Hs::Ladder::LadderTable> HsLadderTables();

// One offer as the window shows it (the account's record against the NPC included).
struct HsLadderOfferView
{
    uint32_t entry = 0;
    std::string name;
    std::string heroClass;  // MAGE ...
    Hs::Ladder::Faction faction = Hs::Ladder::Faction::Any;
    int rank = Hs::Ladder::kBottomRank;
    int stars = 0;
    uint32_t wins = 0, losses = 0, draws = 0; // the account's results against this NPC
};
struct HsLadderStanding
{
    bool onLadder = false; // the account has a ladder entry
    int rank = Hs::Ladder::kBottomRank;
    int stars = 0;
    uint32_t wins = 0, losses = 0, draws = 0;
    uint32_t legendPosition = 0; // 1-based place in the shared ladder, only for Legend
};
// Standing without drawing offers (also while a duel is paused or walking up).
bool HsLadderStandingFor(uint32_t accountId, HsLadderStanding& standing);
// The account's standing and its three offers (drawn now and saved when the stored set was missing or not valid any
// more). False with a player-readable `why` when the ladder cannot offer (see HsLadderChairWhy) or the offers cannot
// be read at all.
bool HsLadderOffersFor(uint32_t accountId, HsLadderStanding& standing, std::vector<HsLadderOfferView>& offers,
                       std::string& why);
// The stored offers of the account contain that NPC (checked again when a pick is confirmed).
bool HsLadderIsOffered(uint32_t accountId, uint32_t npcEntry);

// What a match or a walk-up needs of a ladder NPC (any roster NPC, blocked or not).
struct HsLadderNpc
{
    uint32_t entry = 0;
    std::string name;
    std::string heroClass;
    Hs::Ladder::Faction faction = Hs::Ladder::Faction::Any;
    std::string deckKey;
    Hs::Match::AiProfile profile = Hs::Match::AiProfile::Normal;
    std::vector<std::string> deck; // the library deck, one entry per copy
    bool enabled = true;
    bool blocked = false;          // kept out of the offers by hearthstone_ladder_block
};
// False: no such NPC on the ladder, or its library deck is not known.
bool HsLadderNpcSetup(uint32_t npcEntry, HsLadderNpc& out);
// What an NPC says after a match with that player result ("" when the world data has no line of the kind).
std::string HsLadderLine(Hs::Ladder::Result playerResult, uint32_t seed);

// A ladder match against an offered NPC ended with a real result: applies it (HsLadderPlayerResult, which queues the
// following NPC round) and, when it was applied or queued, clears the account's offers (OffersAfterMatch + save), so
// the next sit-down draws three new ones. A refused result changes nothing (the offers stay).
// DuelId is the ladder duel the result settles (its row is deleted in the transaction that applies the result, so the
// result counts exactly once; 0 for none).
HsLadderResultReply HsLadderMatchResult(uint32_t accountId, std::string const& playerName, uint32_t npcEntry,
                                        Hs::Ladder::Result result, uint64_t duelId = 0);
// The result of that duel waits in the ladder's queue (a round is running): the duel cannot be resumed meanwhile.
bool HsLadderDuelQueued(uint64_t duelId);

// `.hs ladder show|npc|player|result|round|offers|walk|reset` (args[0] == "ladder"; the caller checked security >= MODERATOR).
void HsLadderCommand(ChatHandler* handler, int security, std::vector<std::string> const& args);
void HsLadderUsage(ChatHandler* handler);
// The "Ladder:" line of `.hs status`.
void HsLadderStatus(ChatHandler* handler);

#endif
