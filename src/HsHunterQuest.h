#ifndef MOD_HEARTHSTONE_HS_HUNTER_QUEST_H
#define MOD_HEARTHSTONE_HS_HUNTER_QUEST_H
#include <cstdint>
class Player;
class Creature;
class GossipMenu;
void AddHsHunterQuestScripts();
bool HsHunterGossipActive(Player* player, Creature* creature);
void HsHunterAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender);
bool HsHunterHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender);
void HsHunterAfterQuestChange(Player* player);
void HsHunterForget(uint32_t guid);
void HsHunterUpdate(uint32_t diff);
#endif
