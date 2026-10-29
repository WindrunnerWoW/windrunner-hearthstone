#ifndef MOD_HEARTHSTONE_HS_ALLIANCE_PRIEST_QUEST_H
#define MOD_HEARTHSTONE_HS_ALLIANCE_PRIEST_QUEST_H

#include <cstdint>

class Player;
class Creature;
class GossipMenu;
class WorldPacket;

bool HsAlliancePriestGossipActive(Player* player, Creature* creature);
void HsAlliancePriestAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender);
bool HsAlliancePriestHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender);
void HsAlliancePriestAfterQuestChange(Player* player);
void AddHsAlliancePriestQuestScripts();
bool HsAlliancePriestCanInteractPacket(Player* player, WorldPacket const& packet);
void HsAlliancePriestBeforeTurnIn(Player* player, WorldPacket const& packet);
void HsAlliancePriestAfterTurnIn(Player* player, WorldPacket const& packet);
void HsAlliancePriestForget(uint32_t guid);

#endif
