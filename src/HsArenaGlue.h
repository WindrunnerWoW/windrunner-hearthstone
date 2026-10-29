/*
 * mod-hearthstone: the core-facing glue of the Draft Arena: entry payment, the run and duel
 * lifecycle, the GM commands. The rules, the run state machine and the entry order are pure C++ in src/arena; the
 * database is HsArenaStore.*; this file connects them to the core.
 *
 * The API below is what the host gossip and the protocol ops call. Every function answers an HsArenaReply:
 * ok / why (a refusal code) and the run as it is now. Nothing here talks to the client; the one push is the GAME
 * payload of a started or resumed duel.
 *
 * Threading. Everything that changes a run or a duel (enter, start, resume, retire, the end of a duel, the GM
 * commands) runs under ONE mutex (g_mutex in HsArenaGlue.cpp) that serializes the arena per process: two characters of
 * an account can never advance or hold the same duel at once. The lock order is
 *   arena glue mutex -> session registry mutex (HsSessionHandshaken, pushes) -> match service / run service / database.
 * HsArenaStore's StoreActions / StoreSuspended (called by the match glue under the registry mutex or from the world
 * thread) take no arena glue lock. Entry is called from a gossip hook on the player's map thread (it uses the
 * character database transaction of that thread); the other player-bound functions from the same place or the world
 * thread; the GM commands run on the world thread.
 *
 * Refusal codes (HsArenaReply::why): the ArenaService codes (data, no_run, stale, open_run, special_pending,
 * daily_used, bad_state, unpaid, repair, bad_class, not_offered, picked, bad_card, not_special, not_missing, deck,
 * storage) and the glue's: unavailable (tables, card data or matches missing), funds, debit, commit (the entry could
 * not be paid; nothing was charged and the reservation is released), host (not at Ezra / Ibelin), world (dead, in
 * combat, no addon, group in combat), running (an ordinary
 * match runs), busy (another character holds the duel), paused (a paused duel exists: resume it), ready (no duel to
 * resume), match (the engine refused to create or restore the duel).
 */

#ifndef MOD_HEARTHSTONE_HS_ARENA_GLUE_H
#define MOD_HEARTHSTONE_HS_ARENA_GLUE_H

#include "arena/HsArenaRules.h"
#include "arena/HsArenaService.h"
#include "match/HsMatchService.h"

#include <cstdint>
#include <string>
#include <vector>

class ChatHandler;
class Creature;
class Player;

struct HsArenaReply
{
    bool ok = false;
    std::string why;      // refusal code when !ok
    std::string text;     // a more specific player-readable reason (world, funds); empty: HsArenaWhyText(why)
    bool replay = false;  // the step was applied already (the run is the stored state)
    bool hasRun = false;  // run is filled in
    Hs::ArenaRun run;     // the run after the call
    // StartDuel / ResumeDuel: the first GAME payload, already pushed to the character (kept for logs and tests).
    std::string payload;
};

// The player-readable sentence of a refusal code.
std::string HsArenaWhyText(std::string const& why);

// Hearthstone Masters that host the arena (Ezra Chatterton 919501, Ibelin Redmoore 919502).
bool HsArenaIsHost(uint32_t creatureEntry);
// Whether NEW entries are accepted. Existing runs stay playable either way.
bool HsArenaEnabled();
// The arena is usable: tables, card data and matches loaded.
bool HsArenaReady();

// The account has an arena run with a duel that is paused or running (one card duel per account, a ladder duel
// cannot start meanwhile).
bool HsArenaHasDuel(uint32_t accountId);

// The account's current (newest) run; why = no_run without one. A run whose duel is no longer live in this process
// (InDuel after a crash) is moved to Paused first, so the state it answers is the real one.
HsArenaReply HsArenaGet(uint32_t accountId);

// ---- entry --------------------------------------------------------------------------------------------------------
// Starts a run for the character's account. pay = Gold: takes Hearthstone.Arena.EntryCostCopper (as snapshotted in
// the run) from the character's money; Coins: takes EntryCostCoins Hearthstone Coin items from its bags (not the
// bank); Free: no debit, no daily slot (the arena introduction quest gates it). A paid entry reserves the
// account's daily slot and creates the run in one transaction, then debits the character and confirms the entry in
// a second one that contains BOTH (see HsArenaGlue.cpp for the crash analysis). A retry while a reserved run waits for
// its debit uses that run's ORIGINAL payment method, whatever pay says. host: the Hearthstone Master the character
// talks to (HsArenaIsHost). The run is in ChoosingClass on success, with its class offer.
HsArenaReply HsArenaEnter(Player* player, Creature* host, Hs::ArenaPay pay);
// True when the account has already used today's paid entry (its trader period): from then on it may start practice runs.
bool HsArenaDailySlotUsed(uint32_t accountId);
// GM: an unpaid run for a character (online or not), without the host, the enable switch or the lesson.
HsArenaReply HsArenaStartFree(uint32_t accountId, uint32_t guid);

// ---- drafting -----------------------------------------------------------------------------------------------------
// runId 0 = the current run, otherwise it must be the current run (stale). The offers are stored before they are
// returned; a repeated step answers replay.
HsArenaReply HsArenaChooseClass(uint32_t accountId, uint32_t runId, std::string const& deckClass);
HsArenaReply HsArenaPick(uint32_t accountId, uint32_t runId, uint32_t pickNo, std::string const& cardId);

// ---- duels --------------------------------------------------------------------------------------------------------
// Ready -> InDuel: drafts the AI opponent (a random class, a 30-card draft under the same offer rules, Hard AI) and
// stores it BEFORE the duel exists (a retry after a crash plays the stored one), starts the duel, stores the AI's
// opening mulligan, applies the In Card Match effect, binds the character to the host and pushes the first GAME
// payload. Needs the host, the addon and a character that is alive and out of combat.
HsArenaReply HsArenaStartDuel(Player* player, Creature* host, uint32_t runId);
// Paused -> InDuel: rebuilds the stored duel by replaying its log (the rules version and card dataset it was
// recorded with must be the current ones, else the run goes to Repair), applies the effect, binds to the host and
// pushes the fresh GAME payload. Any character of the account can resume; a second one is refused while another holds
// the duel (busy).
HsArenaReply HsArenaResumeDuel(Player* player, Creature* host, uint32_t runId);
// Explicit retirement (not a pause): ends a live duel of the run (dropped, not scored), then pays the prize of the wins
// so far. guid: the character that retires (it receives the coins). Works from any active state or Repair.
HsArenaReply HsArenaRetire(uint32_t accountId, uint32_t guid, uint32_t runId);

// Called by the match glue for every ended arena duel (HsMatchGlue.cpp LogEnd, once): records the result in the run,
// which ends the run on a loss or the tenth win. An interrupted duel (res int) pauses the run instead.
void HsArenaDuelEnded(Hs::MatchEnd const& end);

// ---- startup and GM -------------------------------------------------------------------------------------------------
// OnStartup, after the match and collection data: hands the card data and dataset hash to the run service, installs
// the duel sink, pauses the runs a crash left in a duel. Without the arena tables the arena stays off.
void HsArenaStartup();

// `.hs arena show|start|retire|repair` (args[0] == "arena"; the caller checked security >= MODERATOR).
void HsArenaCommand(ChatHandler* handler, int security, std::vector<std::string> const& args);
void HsArenaUsage(ChatHandler* handler);
// The "Arena:" line of `.hs status`.
void HsArenaStatus(ChatHandler* handler);

#endif
