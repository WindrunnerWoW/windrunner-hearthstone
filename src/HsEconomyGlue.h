/*
 * mod-hearthstone: the core-facing glue of the economy. The rules (dust values, crafting, match coins) are
 * pure C++ in src/collection (HsWallet.h, HsCollectionService.h); this file family connects them to the core:
 *   - Hearthstone coins are a real item (kHsCoinItem, world migration 0006): a match pays them into the character's
 *     bags (by mail when the bags are full), delivered on the world thread after the match ended;
 *   - the vendor window of a pack vendor sells two booster pack listings: kHsPackItem for gold (the core sells it like
 *     any vendor item) and kHsPackCoinListing, which the module answers itself (CMSG_BUY_ITEM): it takes
 *     Hearthstone.Pack.PriceCoins coin items and gives kHsPackItem instead;
 *   - right-clicking a kHsPackItem (CMSG_OPEN_ITEM) adds an unopened pack to the account (the Packs tab);
 *   - the crafting NPCs (world table hearthstone_vendor, crafting = 1): "Craft and scrap cards" opens the collection in
 *     crafting mode (CRAFT_ACCESS push); CRAFT_MAKE / CRAFT_SCRAP are only accepted while the character stays within
 *     kCraftRange of that NPC on its map; a sweep on the world thread closes the access when it walks away;
 *   - the `.hs wallet` GM commands and the economy line of `.hs status`.
 *
 * Implemented in HsEconomyGlue.cpp. The gossip and packet hooks of HsMatchGlue.cpp call into it (one gossip sender for
 * the module); HearthstoneScripts.cpp implements the pushes (it owns the session registry).
 */

#ifndef MOD_HEARTHSTONE_HS_ECONOMY_GLUE_H
#define MOD_HEARTHSTONE_HS_ECONOMY_GLUE_H

#include "collection/HsWallet.h"
#include "match/HsMatchService.h"

#include <cstdint>
#include <string>
#include <vector>

class ChatHandler;
class Creature;
class GossipMenu;
class Player;
class WorldPacket;

// The items of world migration 0006.
constexpr uint32_t kHsCoinItem = 919601;         // Hearthstone Coin
constexpr uint32_t kHsPackItem = 919602;         // Hearthstone Booster Pack (right-click: an unopened pack)
constexpr uint32_t kHsPackCoinListing = 919603;  // the vendor listing "for coins", answered by the module
// Packs one coin purchase may buy (the stack size of kHsPackItem).
constexpr uint32_t kHsMaxPacksPerBuy = 20;

// Gossip action of the crafting option (sender 0x4853 'HS', see HsMatchGlue.cpp).
constexpr uint32_t kHsActionCraft = 0x48530022u;

// How far (yards, same map) a character may move from the crafting NPC and keep crafting.
constexpr float kCraftRange = 15.0f;

// Startup and config reload: the vendor rows of the world database, the dust values for the collection service.
void HsEconomyStartup();
void HsEconomyReloadConfig();

// Is the creature entry a pack vendor or a crafting NPC (hearthstone_vendor)?
bool HsEconomyIsVendor(uint32_t entry);
// Does the NPC have an option for this player right now (a pack vendor window, or crafting once the player's account
// turned in Byron's trade quest)? False: the module leaves the NPC's gossip alone.
bool HsEconomyHasOptions(Player* player, uint32_t entry);
// Appends the crafting option of a crafting NPC to menu (if it fits). The caller sends the menu.
void HsEconomyAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender);
// A gossip select with the module's sender: true when action was an economy action (handled), false otherwise.
bool HsEconomyHandleSelect(Player* player, Creature* creature, uint32_t action);

// CMSG_BUY_ITEM (before the core): true when the packet was the coin listing and is consumed (bought or refused).
bool HsEconomyHandleBuy(Player* player, WorldPacket const& packet);
// CMSG_OPEN_ITEM (before the core, not during a match): true when the item was a booster pack item and is consumed.
bool HsEconomyHandleOpenItem(Player* player, WorldPacket const& packet);

// Every match end (HsMatchGlue LogEnd): queues the coins of a normally completed match; HsEconomyUpdate delivers them.
void HsEconomyMatchEnded(Hs::MatchEnd const& end);

// A finished Warsong Battlegrounds game queues its coins the same way (the packs are granted by the caller).
void HsEconomyBattlegroundEnded(uint32_t guid, uint32_t accountId, uint32_t coins);

// Hearthstone Coin items in the character's bags (not the bank).
uint32_t HsCoinCount(Player* player);

// The crafting access of the character: true while a crafting NPC is open and the character is near it
// (npcName is then its name).
bool HsCraftAccess(Player* player, std::string& npcName);
// World update: delivers queued match coins, closes the access of characters that left the NPC (CRAFT_ACCESS open=0).
void HsEconomyUpdate(uint32_t diff);
// Logout: forgets the access.
void HsEconomyLogout(uint32_t guidLow);

// `.hs wallet ...` (args[0] == "wallet"; the caller checked security >= MODERATOR).
void HsWalletCommand(ChatHandler* handler, int security, std::vector<std::string> const& args);
void HsWalletUsage(ChatHandler* handler);
// The "Economy:" line of `.hs status`.
void HsEconomyStatus(ChatHandler* handler);

// Implemented in HearthstoneScripts.cpp: pushes to the account's character in the world (if handshaken). Call
// without holding any lock. WALLET_UPD carries the character's coin items and the account's dust.
void HsPushWalletChanged(uint32_t accountId, std::string const& why, uint32_t gain);
void HsPushPackChanged(uint32_t accountId, uint32_t stock);
void HsPushCardChanged(uint32_t accountId, uint32_t version);
void HsPushCraftAccess(uint32_t accountId, bool open, std::string const& npc);
// Resolves "<character>" or "#<account>" for a GM command; false after telling the GM why.
bool HsResolveAccountTarget(ChatHandler* handler, std::string const& arg, uint32_t& accountId, std::string& label);
std::string HsGmSource(ChatHandler* handler);
std::string HsGmName(ChatHandler* handler);

#endif
