/*
 * mod-hearthstone: the ladder chair, its offer window and the walk-up of the opponent. The
 * ladder runtime, the offers and the world data are HsLadderGlue.*; the texts and the walking points that need no core
 * are pure C++ in src/ladder/HsLadderPlay.*.
 *
 * The chair. Gameobject template 919100 (a copy of Wooden Chair 101766, world migration 0049) carries the script
 * hearthstone_ladder_chair. GameObject::Use calls the script's OnGossipHello BEFORE the core's chair code and the script
 * returns false, so the core still seats the player (NearTeleportTo the slot, SetStandState sit): the window opens from
 * the chair and the player sits as on any chair. Only chairs named by a hearthstone_ladder_table row open the window.
 * While the ladder is off, not ready or its start is running the chair says so and the player just sits.
 *
 * The addon window: a client advertising HELLO ladderui=1 gets LADDER_OPEN with structured standing, offers and
 * ready decks. LADDER_PLAY / LADDER_RES / LADDER_GIVE are deferred past the session registry lock, then checked against
 * the remembered chair, seating, reach, unlock, current offers, owned ready decks and the account's duel state. Standing
 * up sends LADDER_CLOSE. Older clients keep the three-page gossip fallback (sender 'LD', world migration 0050).
 *
 * The walk-up (one per table and per character at a time):
 *   WalkIn   the chosen NPC is summoned from its own creature entry at the table's spawn point as a temporary summon:
 *            friendly faction 35 for this one copy, no npc flags, passive, immune to players and creatures, the module's
 *            NullCreatureAI instead of its own AI. It walks Route() to the stop point and then to the opponent chair
 *            (MovePoint, walk speed). After about 20 s it is placed at the end of the route.
 *   Seated   it sits on the opponent chair (SetStandState); the player sits on the ladder chair. After a short pause the
 *            ordinary match is started (HsMatchStartLadder).
 *   InMatch  a ladder match: HsLadderMatchEnded (called by the match glue for every ended match) routes a real result
 *            (win, loss, draw, surrender) to HsLadderMatchResult together with its duel id; every other end leaves the ladder and
 *            the offers alone. The match is a durable duel (src/HsLadderDuelStore.*): logout, teleport, death, the NPC gone
 *            and closing the window PAUSE it (HsLadderMatchSuspended), the opponent walks home and the duel waits.
 *
 * The paused duel. An account with a stored ladder duel gets another window from either chair: Resume the duel, Surrender
 * (a loss), nothing else. Resume summons the stored opponent again (the walk-up is the same), HsMatchResumeLadder rebuilds the
 * duel by replaying its log and the board opens where it was left. Surrender applies a loss without a walk-up.
 *   Parting  the NPC says one line, waits a moment, stands up.
 *   WalkBack it walks back along the points it passed and is unsummoned at the spawn point.
 * Before the match starts the walk-up is cancelled (the NPC walks back, nothing is counted) when the player stands up,
 * moves away, logs out, changes the map, dies, or the match cannot start. A summon that disappears ends the walk-up at
 * once. Server shutdown unsummons every opponent.
 *
 * Threads. The gossip hooks run on the player's map thread: they check, claim the table and summon (the claim and the
 * pending list are behind a leaf mutex). Everything after that (movement, sitting, the match start, the line, the
 * despawn) runs on the world thread in HsLadderTableUpdate, which owns the active walks; the player hooks and the match
 * end only post events (the same leaf mutex). The leaf mutex is never held while the core, the match glue or the ladder
 * glue is called.
 */

#ifndef MOD_HEARTHSTONE_HS_LADDER_TABLE_H
#define MOD_HEARTHSTONE_HS_LADDER_TABLE_H

#include "ladder/HsLadderWorld.h"
#include "match/HsMatchService.h"
#include "protocol/HsSession.h"

#include <cstdint>
#include <string>

class Player;

class HsLadderSession final : public Hs::LadderHandler
{
public:
    explicit HsLadderSession(Player* player) : m_player(player) {}
    void Defer(Hs::Completed const& request) override { m_requests.push_back(request); }
    void Finish(std::vector<std::string>& out);
private:
    Player* m_player;
    std::vector<Hs::Completed> m_requests;
};

// Sends to an already handshaken character. Call without holding a registry lock.
void HsPushLadder(uint32_t guid, std::string const& op, std::string const& payload);
bool HsSessionHasLadderUi(uint32_t guid);

// Registers the chair script (hearthstone_ladder_chair) and the player hooks.
void AddHsLadderTableScripts();

// Every world update: adopts new walk-ups, handles the events posted by the hooks, steps every walk-up.
void HsLadderTableUpdate(uint32_t diff);
// OnShutdown: unsummons every opponent and forgets every walk-up.
void HsLadderTableShutdown();

// Called by the match glue for every match that ended, wherever it ended (a thread of any kind): if it was a ladder match
// its end is queued for the world thread.
void HsLadderMatchEnded(Hs::MatchEnd const& end);
// A ladder duel was suspended (logout, teleport, death, the NPC gone, closing the window ...): its end is queued for the world
// thread as a pause (no result); the opponent walks home. Called by the ladder duel store after it stored the duel.
void HsLadderMatchSuspended(uint32_t guid, char const* why);

// The creature (ObjectGuid raw value) is a walking ladder opponent (the match glue never offers it any gossip).
bool HsLadderIsWalker(uint64_t creatureRaw);
// A table is claimed or a ladder match is running (the ladder cannot be reset now).
bool HsLadderTableBusy();

// .hs ladder walk: the NPC (any creature entry) walks to the table of `table`, says a line, walks back; no match and
// nothing is counted. The GM must be in the world on the table's map (the NPC is summoned next to the GM's map object
// and the GM is the "player" of the walk-up). False with a reason for the GM.
bool HsLadderPreviewWalk(Player* gm, Hs::Ladder::LadderTable const& table, uint32_t npcEntry, std::string& why);

#endif
