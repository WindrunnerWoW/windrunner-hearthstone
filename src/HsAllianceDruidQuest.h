#ifndef MOD_HEARTHSTONE_HS_ALLIANCE_DRUID_QUEST_H
#define MOD_HEARTHSTONE_HS_ALLIANCE_DRUID_QUEST_H

#include <cstdint>
class Player;
class Creature;
class GossipMenu;

bool HsAllianceDruidGossipActive(Player* player, Creature* creature);
void HsAllianceDruidAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender);
bool HsAllianceDruidHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender);
void HsAllianceDruidUpdate(uint32_t diff);
void HsAllianceDruidForget(uint32_t guid);
void HsAllianceDruidQueue(Player* player);

#endif
