/*
 * mod-hearthstone: the host side of the Draft Arena: the arena introduction quest, the gossip of
 * Ezra Chatterton / Ibelin Redmoore, and the ARENA_* protocol ops. The run rules are HsArenaGlue.h, the settlement is
 * HsArenaSettle.h, the wire format is src/arena/HsArenaWire.h, docs/PROTOCOL.md "Draft arena".
 *
 * THE INTRODUCTION QUEST (quests 919921 Horde / 919931 Alliance, world migration 0044; rules in
 * src/quests/HsArenaIntroRules.h). A plain event quest (SpecialFlags 2, no objective rows) that has NO start relation:
 * it is offered through the arena page of the host's gossip while the ACCOUNT has all nine classes unlocked and owns
 * at least 140 different collectible cards. The module sends the quest details and takes the accept packet of these
 * two quests itself (HsArenaHostHandleAccept), because a start relation cannot hold that rule. While the quest is in
 * the log (and the account has had no free run) the arena page offers "start my free run" (HsArenaEnter, pay free).
 * The quest is completed (AreaExploredOrEventHappens) when the newest run of the account is a free run that has ended,
 * whatever its result: by the world update after the run was saved (HsArenaHostUpdate), at login, at the host and at
 * the accept. Paid entry (gold / coins) is offered only when the quest is rewarded on ANY character of the account
 * (read from character_queststatus like HsLessonDone, cached when positive).
 * Once today's paid entry is used the page offers practice runs instead (no cost, no daily slot, no prize).
 *
 * THE GOSSIP. One entry, "The Night Watch Arena": with an open run it opens the arena window (draft, Play, Retire and the
 * special card are all in the window); without one it lists the ways to start a run. Every option is only
 * presentation: each selection re-checks the arena, the host, the quest and the run. The 1.12 gossip packet has no
 * confirmation box and no money field, so prices are plain option text and a confirmation is a second page
 * ("Yes, ..." / "Never mind"). The older run options (next duel, resume, retire, special card) still answer, but no
 * page offers them any more.
 *
 * THE PROTOCOL. HsArenaSession is the Hs::ArenaHandler of one frame. It runs outside the session registry lock
 * (lock order: arena glue mutex -> registry mutex, see HsArenaGlue.h): the handler only records the request and
 * answers Deferred, the caller runs HsArenaSession::Finish after releasing the lock.
 */

#ifndef MOD_HEARTHSTONE_HS_ARENA_HOST_H
#define MOD_HEARTHSTONE_HS_ARENA_HOST_H

#include "arena/HsArenaService.h"
#include "arena/HsArenaWire.h"
#include "protocol/HsSession.h"

#include <cstdint>
#include <string>
#include <vector>

class Creature;
class GossipMenu;
class Player;
class WorldPacket;
class WorldSession;

// ---- gossip (called from HsMatchGlue.cpp, one gossip sender for the module) ----------------------------------------
// Does the host (Ezra / Ibelin) show the arena entry to this player now?
bool HsArenaHostHasOptions(Player* player, Creature* creature);
// Is the arena entry still the way into the introduction quest (no open run and no paid entries unlocked yet)? Then it
// belongs at the bottom of the menu with the other quest-starting options.
bool HsArenaHostIsQuestEntry(Player* player);
// Appends the entry "The Draft Arena" to the menu (if it fits). The caller sends the menu.
void HsArenaHostAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender);
// A gossip select with the module's sender: true when the action belonged to the arena (handled).
bool HsArenaHostHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender);

// ---- the quest ---------------------------------------------------------------------------------------------------------
// CMSG_QUESTGIVER_ACCEPT_QUEST before the core: true when the packet was for one of the two arena quests and is
// consumed (accepted or refused).
bool HsArenaHostHandleAccept(WorldSession* session, WorldPacket const& packet);
// CMSG_QUESTGIVER_CHOOSE_REWARD after the core handled it: an arena quest turned in unlocks paid entry for the account.
void HsArenaHostAfterTurnIn(WorldSession* session, WorldPacket const& packet);
// Has a character of the player's account turned in the arena quest (paid entry unlocked)? Cached when positive.
bool HsArenaPaidUnlocked(Player* player);
// Completes the quest of the character if it holds it and the account's free run has ended. Safe to call at any time.
// True when the quest was completed by this call (the host's quest menu has to be prepared again).
bool HsArenaHostReconcile(Player* player);
// Login / logout hooks: forget the cached answer; reconcile at login.
void HsArenaHostForget(uint32_t accountId);
void HsArenaHostOnLogin(Player* player);

// ---- changes -> ARENA_UPD, quest completion ---------------------------------------------------------------------------
// The run of the account was saved (HsArenaStore.cpp calls it for every committed write; thread safe, cheap).
void HsArenaMarkDirty(uint32_t accountId);
// World update: pushes ARENA_UPD for the marked accounts and reconciles their quest. diff in milliseconds.
void HsArenaHostUpdate(uint32_t diff);

// ---- protocol ------------------------------------------------------------------------------------------------------------
// The view of the account's run for this character (guid 0: no character, live/mine false). Takes the arena glue mutex
// through HsArenaGet: call without holding the session registry mutex.
Hs::ArenaView HsArenaMakeView(uint32_t accountId, uint32_t guid);

// ARENA_GET / ARENA_CLASS / ARENA_PICK / ARENA_CLAIM / ARENA_DUEL / ARENA_RETIRE of one frame (see the header comment:
// Deferred, then Finish). ARENA_DUEL and ARENA_RETIRE (the Play and Retire buttons of the arena window) are accepted
// only while the player is in interaction range of the host whose gossip opened the window (why=host otherwise); they
// start or resume the duel / retire the run exactly like the gossip options.
class HsArenaSession : public Hs::ArenaHandler
{
public:
    HsArenaSession(Player* player, uint32_t accountId, uint32_t guid) : m_player(player), m_account(accountId), m_guid(guid) {}

    Hs::ArenaReply Get(std::string const& reqId) override;
    Hs::ArenaReply Mutate(Hs::ArenaRequest const& request) override;

    // After OnFrame, WITHOUT any lock: runs the recorded request and appends its answer frames to out.
    void Finish(std::vector<std::string>& out);
    bool Pending() const { return m_pending; }

private:
    Hs::ArenaReply Run();

    Player* m_player;
    uint32_t m_account;
    uint32_t m_guid;
    bool m_pending = false;
    bool m_isGet = false;
    Hs::ArenaRequest m_request;
};

// Implemented in HearthstoneScripts.cpp: pushes to the account's character in the world (handshaken). open = ARENA_OPEN
// (the addon opens its arena screen), else ARENA_UPD. Builds the view with HsArenaMakeView, outside every lock.
void HsPushArena(uint32_t accountId, bool open);
// The guid of the account's character in the world, or 0 (HearthstoneScripts.cpp).
uint32_t HsOnlineCharacterOf(uint32_t accountId);
// Implemented in HearthstoneScripts.cpp: ARENA_CLOSE to the character (handshaken): it left the host that opened the
// arena window.
void HsPushArenaClose(uint32_t guidLow);

#endif
