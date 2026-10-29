/*
 * mod-hearthstone: the Warsong Battlegrounds unlock quests 920300 / 920301 (world migration 0060).
 * They are breadcrumbs at the two Hearthstone Masters: Ezra Chatterton (Horde) sends the character to the Warsong Outrider,
 * Ibelin Redmoore (Alliance) to the Silverwing Sentinel. Each quest is ended only by that host. It has NO start relation: the
 * masters offer it through a gossip option, and the module sends the quest details and takes the accept packet itself (the same
 * way as the ladder breadcrumb, HsLadderBreadcrumb.h). It has no objectives and no reward. Turning it in opens the hosts'
 * "How does it work?" tutorial (HsBgGlue.cpp) and their second quest.
 *
 * The second quest of each chain (Hs::kBgRulesQuests, "The Rules of the Field", world migration 0062) is a plain quest of the
 * host, given and ended by it. The last click of the host's tutorial completes it (like the ladder's "The Rules of the
 * League"). Turning IT in opens the "Play" option and the weekly Battlegrounds quests (migration 0061).
 */

#ifndef MOD_HEARTHSTONE_HS_BG_BREADCRUMB_H
#define MOD_HEARTHSTONE_HS_BG_BREADCRUMB_H

#include <cstdint>

class Creature;
class GossipMenu;
class Player;
class WorldPacket;

namespace Hs
{
    inline constexpr uint32_t kBgBreadcrumbGivers[2] = { 919501, 919502 };  // Ezra Chatterton, Ibelin Redmoore
    inline constexpr uint32_t kBgBreadcrumbQuests[2] = { 920300, 920301 };  // the quest of the giver at the same index
    // The second quest of each chain: Horde (Warsong Outrider), Alliance (Silverwing Sentinel).
    inline constexpr uint32_t kBgRulesQuests[2] = { 920302, 920303 };
}

// Is the creature's quest offered to this player now? The creature is Ezra or Ibelin, the mode is enabled, the character
// finished the Hearthstone lesson and has never had either of the two quests.
bool HsBgBreadcrumbOffered(Player* player, Creature* creature);
// Appends the option about the Battlegrounds (the first of two steps) when it is offered (if it fits). The caller sends the menu.
void HsBgBreadcrumbAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender);
// A gossip select with the module's sender: true when the action was the breadcrumb option (handled).
bool HsBgBreadcrumbHandleSelect(Player* player, Creature* creature, uint32_t action);
// CMSG_QUESTGIVER_ACCEPT_QUEST before the core: true when the packet was for one of the two quests and is consumed.
bool HsBgBreadcrumbHandleAccept(Player* player, WorldPacket const& packet);

#endif
