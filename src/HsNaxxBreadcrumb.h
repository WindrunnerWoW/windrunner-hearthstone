/*
 * mod-hearthstone: the Naxxramas breadcrumb quest 919932. It has NO start relation: Ezra and Ibelin offer it through a gossip option, and the module sends the
 * quest details and takes the accept packet itself (the same way as the arena introduction quest, HsArenaHost.h).
 */

#ifndef MOD_HEARTHSTONE_HS_NAXX_BREADCRUMB_H
#define MOD_HEARTHSTONE_HS_NAXX_BREADCRUMB_H

#include <cstdint>

class Creature;
class GossipMenu;
class Player;
class WorldPacket;

// Is the quest offered by this creature to this player now? The creature is Ezra or Ibelin, the character has never had
// the quest, and the account has an ended arena run with at least Hs::kNaxxMinArenaWins wins. False on a query failure.
bool HsNaxxBreadcrumbOffered(Player* player, Creature* creature);
// Appends the option "I overheard you talking about the Chapel of Light." when it is offered (if it fits). The caller sends the menu.
void HsNaxxBreadcrumbAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender);
// A gossip select with the module's sender: true when the action was the breadcrumb option (handled).
bool HsNaxxBreadcrumbHandleSelect(Player* player, Creature* creature, uint32_t action);
// CMSG_QUESTGIVER_ACCEPT_QUEST before the core: true when the packet was for quest 919932 and is consumed.
bool HsNaxxBreadcrumbHandleAccept(Player* player, WorldPacket const& packet);

#endif
