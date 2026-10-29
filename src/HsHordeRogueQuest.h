#ifndef MOD_HEARTHSTONE_HS_HORDE_ROGUE_QUEST_H
#define MOD_HEARTHSTONE_HS_HORDE_ROGUE_QUEST_H

#include <cstdint>
class Player;
class Creature;
class GossipMenu;
class WorldPacket;

bool HsHordeRogueGossipActive(Player* player, Creature* creature);
void HsHordeRogueAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender);
bool HsHordeRogueHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender);
bool HsHordeRogueHandleHello(Player* player, Creature* creature, uint32_t sender);
bool HsHordeRogueCanVendorPacket(Player* player, WorldPacket const& packet);
void HsHordeRogueAfterQuestChange(Player* player);
void HsHordeRogueRefreshObjectives(Player* player, bool force = false);
void HsHordeRogueForgetObjectives(uint32_t guid);
bool HsHordeRogueCanSendQuest(Player* player, WorldPacket const& packet);

#endif
