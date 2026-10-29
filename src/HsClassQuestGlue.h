/* Class unlock quests: native quest log, gossip objectives and duel credit. */
#ifndef MOD_HEARTHSTONE_HS_CLASS_QUEST_GLUE_H
#define MOD_HEARTHSTONE_HS_CLASS_QUEST_GLUE_H

#include "quests/HsClassQuestRules.h"

class Creature;
class GossipMenu;
class Player;

bool HsClassQuestGossipActive(Player* player, Creature* creature);
void HsClassQuestAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender);
bool HsClassQuestHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender);
// Checked at every match menu / start, including stale selections. Other opponents are unaffected.
bool HsClassQuestOpponentAvailable(Player* player, uint32_t entry);
void HsClassQuestMatchStarted(Player* player, Creature* creature, uint32_t matchId);
// Packet / service hooks enqueue only. Player and creature changes happen in the world update.
void HsClassQuestMatchEnded(Hs::MatchEnd const& end);
void HsClassQuestUpdate();
void HsClassQuestForget(uint32_t guid);
void HsClassQuestInvalidateQuest(uint32_t guid, uint32_t quest);
// After a quest-log removal: an abandoned final quest no longer receives a running or queued duel's credit.
void HsClassQuestAfterQuestChange(Player* player);

#endif
