#ifndef MOD_HEARTHSTONE_HS_ALLIANCE_PALADIN_QUEST_H
#define MOD_HEARTHSTONE_HS_ALLIANCE_PALADIN_QUEST_H

#include <cstdint>

class Player;
class Creature;
class GossipMenu;

bool HsAlliancePaladinGossipActive(Player* player, Creature* creature);
void HsAlliancePaladinAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender);
bool HsAlliancePaladinHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender);
void HsAlliancePaladinAfterQuestChange(Player* player);

#endif
