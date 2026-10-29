/*
 * mod-hearthstone: the ladder breadcrumb quests 919970 / 919971.
 * They are DUMMY quests (the text is a placeholder, reviewed one quest at a time later): Ezra Chatterton (Horde) sends the character to
 * The Firebat in Thunder Bluff, Ibelin Redmoore (Alliance) to Amaz Chan in Darnassus. Each quest is ended only by that NPC. It has NO start
 * relation: the two Hearthstone Masters offer it through a gossip option, and the module sends the quest details and takes the
 * accept packet itself (the same way as the Naxxramas breadcrumb, HsNaxxBreadcrumb.h). It has no objectives and no reward.
 *
 * The second quest of each chain (Hs::kLadderRulesQuests) is a plain quest of the committee NPC. Turning IT in unlocks the ladder
 * chairs for the character (a game master may always use them). The tutorial pages are
 * database gossip of the two committee NPCs (world migration 0052).
 */

#ifndef MOD_HEARTHSTONE_HS_LADDER_BREADCRUMB_H
#define MOD_HEARTHSTONE_HS_LADDER_BREADCRUMB_H

#include <cstdint>

class Creature;
class GossipMenu;
class Player;
class WorldPacket;

namespace Hs
{
    // Two quests, one per Hearthstone Master, each ended by the committee NPC of the master's own faction (neither text names
    // the other faction's NPC): Ezra Chatterton (Horde) sends the character to The Firebat in Thunder Bluff (quest 919970),
    // Ibelin Redmoore (Alliance) to Amaz Chan in Darnassus (quest 919971). Both committee NPCs are neutral.
    inline constexpr uint32_t kLadderBreadcrumbGivers[2] = { 919501, 919502 };  // Ezra Chatterton, Ibelin Redmoore
    inline constexpr uint32_t kLadderBreadcrumbQuests[2] = { 919970, 919971 };  // the quest of the giver at the same index
    // The second quest of each chain, "The Rules of the League" (given and ended by the committee NPC of the chain, completed by the
    // last click of its tutorial, world migration 0055). Turning it in unlocks the ladder chairs.
    inline constexpr uint32_t kLadderRulesQuests[2] = { 919972, 919973 };
    // The ladder opens for an account after this many paid arena entries (gold or coins; free and practice runs do not count).
    inline constexpr uint32_t kLadderArenaEntries = 5;
}

// Is the creature's quest offered to this player now? The creature is Ezra or Ibelin, the ladder is enabled, the
// character finished the Hearthstone lesson, its account has made Hs::kLadderArenaEntries paid arena entries and the
// character has never had either of the two quests.
bool HsLadderBreadcrumbOffered(Player* player, Creature* creature);
// Appends the option "I heard there is some kind of league?" (the first of two steps) when it is offered (if it fits). The caller sends the menu.
void HsLadderBreadcrumbAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender);
// A gossip select with the module's sender: true when the action was the breadcrumb option (handled).
bool HsLadderBreadcrumbHandleSelect(Player* player, Creature* creature, uint32_t action);
// CMSG_QUESTGIVER_ACCEPT_QUEST before the core: true when the packet was for one of the two quests and is consumed.
bool HsLadderBreadcrumbHandleAccept(Player* player, WorldPacket const& packet);

// The character may use the ladder chairs: its account made Hs::kLadderArenaEntries paid arena entries, it turned in one of the two
// "Rules of the League" quests, or it is a game master.
bool HsLadderChairUnlocked(Player* player);

#endif
