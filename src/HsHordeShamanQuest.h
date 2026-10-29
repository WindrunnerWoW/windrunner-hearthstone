#ifndef MOD_HEARTHSTONE_HS_HORDE_SHAMAN_QUEST_H
#define MOD_HEARTHSTONE_HS_HORDE_SHAMAN_QUEST_H

#include <cstdint>
class Player;
class Creature;
class GossipMenu;
class WorldPacket;

void AddHsHordeShamanQuestScripts();
bool HsHordeShamanGossipActive(Player* player, Creature* creature);
bool HsHordeShamanNeedsEmptyMentorGossip(Player* player, Creature* creature);
void HsHordeShamanAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender);
bool HsHordeShamanHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender);
bool HsHordeShamanCanInteractPacket(Player* player, WorldPacket const& packet);
void HsHordeShamanBeforeTurnIn(Player* player, WorldPacket const& packet);
void HsHordeShamanAfterTurnIn(Player* player, WorldPacket const& packet);
void HsHordeShamanForget(uint32_t guid);

#endif
