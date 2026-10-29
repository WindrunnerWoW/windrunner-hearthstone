/* Naxxramas quests: the boss stand-in gate, the class challenge option and the match credit. */
#ifndef MOD_HEARTHSTONE_HS_NAXX_QUEST_GLUE_H
#define MOD_HEARTHSTONE_HS_NAXX_QUEST_GLUE_H

#include "match/HsEncounters.h"
#include "match/HsMatchService.h"
#include "quests/HsNaxxQuestRules.h"

class Creature;
class GossipMenu;
class Player;

// ---- boss stand-ins and class challenges (gossip) ----------------------------------------------------------

// Checked at every match menu / start, including stale selections. A boss stand-in (920000-920014) offers its normal
// match only while the player has that boss's quest incomplete; every other entry is unaffected (true).
bool HsNaxxOpponentAvailable(Player* player, uint32_t entry);
// The stand-in has a challenge option for the player: a challenge fought against its encounter whose quest is incomplete.
bool HsNaxxChallengeGossipActive(Player* player, Creature* creature);
// Adds "Begin the <Class> class challenge." for each of them (gossip action block 0x48530401-0x4853041d).
void HsNaxxAppendChallengeGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender);
// True when action belongs to the challenge block (whether or not the challenge is still on offer).
bool HsNaxxIsChallengeAction(uint32_t action);
// The challenge behind a challenge action, or nullptr when it is not on offer (any more) at this creature.
Hs::Challenge const* HsNaxxChallengeOfAction(Player* player, Creature* creature, uint32_t action);
// The challenge may be fought at this creature right now (its quest is incomplete). Checked again by the match start.
bool HsNaxxChallengeAvailable(Player* player, uint32_t entry, Hs::Challenge const& challenge);

// ---- visibility --------------------------------------------------------------------------------------------

// Registers the hook that hides a boss stand-in from everyone it is not currently for (see Hs::NaxxStandInVisible).
void AddHsNaxxQuestScripts();
// After a quest accept / abandon / reward or a credited win: re-evaluates which stand-ins this player sees.
void HsNaxxRefreshVisibility(Player* player);

// ---- credit ------------------------------------------------------------------------------------------------

// A match against a stand-in has started (challenge = nullptr for the normal boss match). Records which quest this
// match can credit and whether it was incomplete now. Other creatures are ignored.
void HsNaxxMatchStarted(Player* player, Creature* creature, uint32_t matchId, Hs::Challenge const* challenge);
// Match thread: enqueues only. Player changes happen in HsNaxxUpdate on the world thread.
void HsNaxxMatchEnded(Hs::MatchEnd const& end);
void HsNaxxUpdate();
void HsNaxxForget(uint32_t guid);
// After a quest-log removal: an abandoned quest no longer receives a running or queued match's credit.
void HsNaxxAfterQuestChange(Player* player);

#endif
