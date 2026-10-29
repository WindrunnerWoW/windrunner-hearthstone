/*
 * mod-hearthstone: the Curse of Naxxramas card items. Right-clicking a card item (919652-919681, the
 * items of src/quests/HsNaxxQuestData.h) adds its card to the account once per account; a replay gives a booster
 * pack (the first item of each wing quest) or nothing.
 *
 * Implemented in HsNaxxItems.cpp. The claim marker and the card grant commit in one character database transaction
 * (HsNaxxStore.h).
 */

#ifndef MOD_HEARTHSTONE_HS_NAXX_ITEMS_H
#define MOD_HEARTHSTONE_HS_NAXX_ITEMS_H

class Player;
class WorldPacket;

// CMSG_OPEN_ITEM (right-click, before the core, not during a match): true when the item was a Naxxramas card item and
// was handled (used up, or kept with the reason told to the player), false for any other item.
bool HsNaxxHandleOpenItem(Player* player, WorldPacket const& packet);

#endif
