#ifndef MOD_HEARTHSTONE_HS_HORDE_PALADIN_QUEST_H
#define MOD_HEARTHSTONE_HS_HORDE_PALADIN_QUEST_H

#include <cstdint>
class Player;
class Creature;
class GossipMenu;
class WorldPacket;

bool HsHordePaladinGossipActive(Player* player, Creature* creature);
void HsHordePaladinAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender);
bool HsHordePaladinHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender);

bool HsHordePaladinCanInteractPacket(Player* player, WorldPacket const& packet);
void HsHordePaladinUpdate(uint32_t diff);
void HsHordePaladinForget(uint32_t guid);
void HsHordePaladinAfterQuestChange(Player* player);

#endif
