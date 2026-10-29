#ifndef MOD_HEARTHSTONE_CARD_TRADER_H
#define MOD_HEARTHSTONE_CARD_TRADER_H
#include <cstdint>
class Creature;
class GossipMenu;
class Player;
// Resets the cached daily card (config reload) and checks the purchase table.
void HsCardTraderLoad();
// Adds "Today's card" when the creature is a registered trader the character has unlocked.
void HsCardTraderAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender);
bool HsCardTraderHandleSelect(Player* player, Creature* creature, uint32_t action);
#endif
