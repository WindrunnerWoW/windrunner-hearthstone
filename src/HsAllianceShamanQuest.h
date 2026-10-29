#ifndef MOD_HEARTHSTONE_HS_ALLIANCE_SHAMAN_QUEST_H
#define MOD_HEARTHSTONE_HS_ALLIANCE_SHAMAN_QUEST_H
#include <cstdint>
class Player;
class Creature;
class GossipMenu;
class WorldPacket;
bool HsAllianceShamanGossipActive(Player* player, Creature* creature);
void HsAllianceShamanAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender);
bool HsAllianceShamanHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender);
bool HsAllianceShamanCanInteractPacket(Player* player, WorldPacket const& packet);
void HsAllianceShamanUpdate(uint32_t diff);
void HsAllianceShamanForget(uint32_t guid);
void HsAllianceShamanAfterQuestChange(Player* player);
void AddHsAllianceShamanQuestScripts();
#endif
