#ifndef MOD_HEARTHSTONE_HS_ALLIANCE_ROGUE_QUEST_H
#define MOD_HEARTHSTONE_HS_ALLIANCE_ROGUE_QUEST_H
#include <cstdint>
class Player;
class Creature;
class GossipMenu;
bool HsAllianceRogueGossipActive(Player* player, Creature* creature);
void HsAllianceRogueAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender);
bool HsAllianceRogueHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender);
void HsAllianceRogueAfterQuestChange(Player* player);
void HsAllianceRogueUpdate(uint32_t diff);
void AddHsAllianceRogueQuestScripts();
#endif
