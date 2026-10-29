#ifndef MOD_HEARTHSTONE_HS_HORDE_MAGE_QUEST_H
#define MOD_HEARTHSTONE_HS_HORDE_MAGE_QUEST_H

#include <cstdint>
class Player;
class Creature;
class GossipMenu;

bool HsHordeMageExperimentsOpen(Player* player, Creature* creature);
bool HsHordeMageGossipActive(Player* player, Creature* creature);
void HsHordeMageAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender);
bool HsHordeMageHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender);
void HsHordeMageUpdate(uint32_t diff);
void HsHordeMageForget(uint32_t guid);

#endif
