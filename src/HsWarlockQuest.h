#ifndef MOD_HEARTHSTONE_HS_WARLOCK_QUEST_H
#define MOD_HEARTHSTONE_HS_WARLOCK_QUEST_H

#include <cstdint>
class Player;
class Creature;
class GossipMenu;

bool HsWarlockGossipActive(Player* player, Creature* creature);
void HsWarlockAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender);
bool HsWarlockHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender);
void HsWarlockAfterQuestChange(Player* player);

#endif
