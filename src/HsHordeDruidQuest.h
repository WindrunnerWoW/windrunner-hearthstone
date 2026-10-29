#ifndef MOD_HEARTHSTONE_HS_HORDE_DRUID_QUEST_H
#define MOD_HEARTHSTONE_HS_HORDE_DRUID_QUEST_H

#include <cstdint>
class Player;
class Creature;
class GossipMenu;

bool HsHordeDruidGossipActive(Player* player, Creature* creature);
void HsHordeDruidAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender);
bool HsHordeDruidHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender);
void HsHordeDruidUpdate(uint32_t diff);
void HsHordeDruidReconcileFeed(Player* player);
void HsHordeDruidAfterQuestChange(Player* player);
void AddHsHordeDruidQuestScripts();

#endif
