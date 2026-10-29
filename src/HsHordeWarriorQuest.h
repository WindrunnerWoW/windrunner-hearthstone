#ifndef MOD_HEARTHSTONE_HS_HORDE_WARRIOR_QUEST_H
#define MOD_HEARTHSTONE_HS_HORDE_WARRIOR_QUEST_H

#include <cstdint>
class Player;
class Creature;
class GossipMenu;

bool HsHordeWarriorSceneOpen(Player* player, Creature* creature);
void HsHordeWarriorAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender);
bool HsHordeWarriorHandleSelect(Player* player, Creature* creature, uint32_t action);
void HsHordeWarriorUpdate(uint32_t diff);
void HsHordeWarriorForget(uint32_t guid);
void HsHordeWarriorAfterQuestChange(Player* player);
void AddHsHordeWarriorQuestScripts();

#endif
