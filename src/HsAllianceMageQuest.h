#ifndef MOD_HEARTHSTONE_HS_ALLIANCE_MAGE_QUEST_H
#define MOD_HEARTHSTONE_HS_ALLIANCE_MAGE_QUEST_H

#include <cstdint>

class Player;
class Creature;
class GossipMenu;

void AddHsAllianceMageQuestScripts();
bool HsAllianceMageGossipActive(Player* player, Creature* creature);
void HsAllianceMageAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender);
bool HsAllianceMageHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender);
void HsAllianceMageAfterQuestChange(Player* player);

#endif
