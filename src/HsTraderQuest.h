#ifndef MOD_HEARTHSTONE_HS_TRADER_QUEST_H
#define MOD_HEARTHSTONE_HS_TRADER_QUEST_H

class Player;
class WorldPacket;

// Drops the core's duplicate item-objective push for vendored objective items (HsTraderQuestRules.h).
// The saved item count, completion checks and turn-in requirements are untouched.
bool HsTraderQuestCanSendPacket(Player* player, WorldPacket const& packet);

#endif
