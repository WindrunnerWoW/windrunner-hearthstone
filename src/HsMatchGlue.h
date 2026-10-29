/*
 * mod-hearthstone: the core-facing glue of the matches.
 * The rules, the AI and the match service are pure C++ in src/match; the
 * protocol is in src/protocol. This file family connects them to the core:
 * the opponent gossip option, difficulty and deck menus (the opponent
 * registry of src/match/HsOpponents.h, loaded from the world database at startup
 * and on a config reload), the match start and its
 * validation, the optional "In Card Match" aura, the server-side action block
 * with the Help > Stuck intercept, every end condition, the pushes, and the
 * `.hs match` GM commands.
 *
 * Implemented in HsMatchGlue.cpp. HearthstoneScripts.cpp owns the session
 * registry and calls into this file; this file calls back HsSessionHandshaken.
 */

#ifndef MOD_HEARTHSTONE_HS_MATCH_GLUE_H
#define MOD_HEARTHSTONE_HS_MATCH_GLUE_H

#include "match/HsMatchService.h"
#include "match/HsOpponents.h"
#include "protocol/HsSession.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class ChatHandler;
class Creature;
class Player;

// Monotonic 64-bit milliseconds (the clock of the sessions and the match service).
uint64_t HsNowMs();

// ---- Draft Arena duels: the seam to the arena storage ------------------------------
// An arena duel (MatchStartParams::arena) is never lost to an interruption: logout, teleport, death, the host
// NPC leaving the world, a GM and a config reload SUSPEND it (MatchService::Suspend: setup, action log, remaining
// timer) instead of ending it, and the protection aura is released as for an ordinary end. What happens to the log
// is behind this interface: HsArenaDbStorage (src/HsArenaStore.h) is installed as the sink at startup; before
// that (or without the arena tables) the default one stores nothing (StoreActions succeeds, StoreSuspended only
// writes a log line).
//
// Threads: StoreActions of a GAME_ACT runs under the session registry mutex (the same place the match service is
// called from), StoreActions of an AI action and StoreSuspended run on the world thread, the thread of the
// player's hook or (a host NPC that left) the world thread after the map update. The sink takes no lock the glue
// holds, must not call back into the match service and should not block for long.
class HsArenaDuelSink
{
public:
    virtual ~HsArenaDuelSink() = default;

    // The new log entries of a duel's newest action (index first.. of the run's log). Called BEFORE the payload
    // is sent. false = not stored: the glue then suspends the duel with the entries stored so far and shows
    // nothing of the action, so the player never sees progress that is not saved.
    virtual bool StoreActions(uint32_t guid, uint64_t runId, uint32_t first,
                              std::vector<Hs::LoggedAction> const& actions) = 0;

    // A duel that was suspended (why: logout, teleport, death, npc, gm, stuck, store ...). duel.params.arenaRunId
    // names the run, duel.log is everything stored by StoreActions, duel.timerMs the remaining turn timer.
    virtual void StoreSuspended(Hs::SuspendedDuel const& duel, char const* why) = 0;
};

// Installs the sink (nullptr: back to the default). The sink must outlive its installation.
void HsSetArenaDuelSink(HsArenaDuelSink* sink);

// What HsArenaGlue.cpp needs from this file to start and resume arena duels (the aura, the binding to the
// host NPC, the push and the opponent names are internal here).
// Applies the In Card Match effect and binds the character's duel to the host NPC (the NPC leaving the world suspends
// the duel). False: the effect is configured and could not be applied (nothing is left behind).
bool HsMatchAttachArena(Player* player, Creature* host);
// Removes the effect and the binding (idempotent). player may be null (looked up when still in the world).
void HsMatchDetachArena(Player* player, uint32_t guid);
// Pushes a GAME payload to the character when it is handshaken and in the world (player may be null).
void HsMatchPushGame(Player* player, uint32_t guid, std::string const& payload);
// A random NPC name of the faction (hearthstone_finder_npc), as the match finder picks it; "" when there is none.
std::string HsFinderOpponentName(bool alliance);
// The deck library as loaded at startup (immutable; null before HsMatchStartup). The ladder reads the library decks.
std::shared_ptr<Hs::DeckLibrary const> HsMatchLibrary();

// ---- World NPC Ladder: the seam to the ladder chair (src/HsLadderTable.cpp) ---------------------------------
// A ladder match is an ordinary match (same rules, aura, coins of the opponent's difficulty) against a summoned
// creature, with the NPC's library deck and AI profile instead of an opponent registry pick. It is not a quest duel
// (no quest credit hooks). It is a durable duel like an arena duel (MatchStartParams::arena with a run id that
// has Hs::Ladder::kLadderRunBit, stored by HsLadderDuelStore.h): an interruption pauses it, it is resumed at a chair.
struct HsLadderMatchSetup
{
    uint32_t npcEntry = 0;                 // the creature entry (the ladder entrant id) of the opponent
    std::string opponentName;              // the creature's name
    std::string opponentClass;             // MAGE ...
    std::string opponentDeckKey;           // library deck key (for the history row)
    std::vector<std::string> opponentDeck; // one entry per copy
    Hs::Match::AiProfile profile = Hs::Match::AiProfile::Normal; // Normal AI: difficulty normal, Hard AI: hard
};
// "" when the player may start a match now, else why not (the checks of every ordinary match start: matches enabled,
// the addon is running, not already in a match, the lesson done, alive, not in combat, nobody in the group in combat).
std::string HsMatchStartRefusal(Player* player);
// The character's decks that are ready for a match (at most 20, id and menu label). False with the reason.
bool HsMatchReadyDecks(Player* player, std::vector<std::pair<uint32_t, std::string>>& out, std::string& why);
// Starts the ladder match of the player's deck deckId against the summoned creature. Returns "" on success (matchId
// filled, the first GAME payload pushed, the effect applied, the creature bound: it leaving the world ends the match)
// or the reason it was refused (nothing is left behind).
std::string HsMatchStartLadder(Player* player, Creature* opponent, uint32_t deckId, HsLadderMatchSetup const& setup,
                               uint32_t& matchId);
// A ladder duel is a durable match (see HsLadderDuelStore.h): every action is stored before the player sees it and an
// interruption (logout, teleport, death, the NPC gone, closing the window) PAUSES it. Only an explicit surrender is a loss.
// "" when the player may resume a paused ladder duel now (the prerequisites of a match start; the duel of the account is not
// the obstacle), else why not.
std::string HsMatchResumeRefusal(Player* player);
// Rebuilds the stored duel duelId by replaying its log and plays it on against the summoned creature. Returns "" on success
// (matchId filled, the board pushed, the effect applied and the creature bound) or the reason it was refused (the duel stays
// stored). endedRes is filled ("win", "loss", "draw") when the log replays to the end of the duel (the server stopped
// between the last action and the result): nothing is protected then, the caller applies that result.
std::string HsMatchResumeLadder(Player* player, Creature* opponent, uint64_t duelId, uint32_t& matchId, std::string& endedRes);

// Implemented in HearthstoneScripts.cpp: the character has a session in the registry that
// completed the HELLO handshake. Takes the registry mutex.
bool HsSessionHandshaken(uint32_t guidLow);

// GAME_GET / GAME_ACT / GAME_LEAVE of one character, handed to Hs::Session::OnFrame. It
// records a match that ended with the action; the caller then runs HsMatchFinishFrame
// WITHOUT holding any lock (removing the aura and logging touch the core).
class HsMatchSession : public Hs::MatchHandler
{
public:
    HsMatchSession(uint32_t guidLow, uint64_t nowMs) : m_guid(guidLow), m_now(nowMs) {}

    std::string GameGet() override;
    Hs::GameReply GameAct(Hs::GameActRequest const& request) override;
    Hs::GameReply GameLeave(uint32_t matchId) override;
    // GAME_PAUSE (and a GAME_LEAVE of a running arena duel, which is never a concede): saves and suspends the
    // arena duel; the cleanup is HsMatchFinishFrame. Ordinary matches refuse with why=ordinary.
    Hs::GameReply GamePause(uint32_t matchId) override;

    bool Ended() const { return m_ended; }
    Hs::MatchEnd const& End() const { return m_end; }
    // The arena store failed for the action: the duel was suspended (the cleanup is HsMatchFinishFrame).
    bool Suspended() const { return m_suspended; }
    Hs::SuspendedDuel const& Duel() const { return m_duel; }
    // Why the duel was suspended: "store" (the store failed) or "pause" (the player closed the duel window).
    char const* SuspendWhy() const { return m_suspendWhy; }

private:
    uint32_t m_guid;
    uint64_t m_now;
    bool m_ended = false;
    Hs::MatchEnd m_end;
    bool m_suspended = false;
    char const* m_suspendWhy = "store";
    Hs::SuspendedDuel m_duel;
};

// Cleanup after an OnFrame that ended the character's match (concede) or suspended it (arena): the aura, the log.
void HsMatchFinishFrame(Player* player, HsMatchSession const& session);

// The usable quest NPC opponents (entry, unlock quest, faction) of the loaded registry; empty with matches
// disabled.
std::vector<Hs::OpponentRegistry::UnlockNpc> HsQuestNpcs();

// The starter recipe of a class (library deck starter_<class>, kind starter, ready). False when the
// library is not loaded or the deck is missing or not ready.
bool HsStarterRecipe(std::string const& deckClass, Hs::DeckCards& cards, std::string& name);

// Implemented in HearthstoneScripts.cpp: awards the starter deck of a class to an account: the protected
// top-up of its cards (idempotent, every call), then once per account the deck (when the list has room) and the
// award record; pushes COLL_CHANGED / DECK_CHANGED to the account's online character and logs. Never unlocks the
// class. text is a player-readable summary; returns false on a failure (nothing or only the cards were written).
// source is the ledger source (gm:5, char:42, unlock:...).
bool HsAwardStarter(uint32_t accountId, std::string const& deckClass, std::string const& source, std::string& text);

// Implemented in HearthstoneScripts.cpp: unlocks a class for the whole account (like .hs class unlock;
// pushes DECK_CHANGED) and then awards its starter deck (HsAwardStarter). An already unlocked class is fine (the award
// is idempotent). text is a player-readable summary; false when the unlock or the award failed.
bool HsUnlockClass(uint32_t accountId, std::string const& deckClass, std::string const& source, std::string& text);

// WorldScript hooks.
void HsMatchStartup();                // OnStartup: card database, engine support, deck library, opponents, aura check
void HsMatchReloadConfig();           // OnAfterConfigLoad(reload)
void HsMatchUpdate();                 // every world update: the AI pacing tick

// `.hs match show|end|decks` (args[0] == "match"; the caller checked security >= MODERATOR).
void HsMatchCommand(ChatHandler* handler, int security, std::vector<std::string> const& args);
void HsMatchUsage(ChatHandler* handler);
// The "Matches:" line(s) of `.hs status`.
void HsMatchStatus(ChatHandler* handler);

// Registers the match scripts (player hooks, server packet hooks, creature and unit hooks).
void AddHearthstoneMatchScripts();

#endif
