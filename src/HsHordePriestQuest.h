#ifndef MOD_HEARTHSTONE_HS_HORDE_PRIEST_QUEST_H
#define MOD_HEARTHSTONE_HS_HORDE_PRIEST_QUEST_H

#include <cstdint>

class Player;
void AddHsHordePriestQuestScripts();
void HsHordePriestUpdate(uint32_t diff);
void HsHordePriestAfterQuestChange(Player* player);

#endif
