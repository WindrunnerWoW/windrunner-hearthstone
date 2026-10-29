/*
 * mod-hearthstone: the introduction questline. The quests, the students and their card items
 * are rows of world migration 0008. The module hands out what the
 * quests themselves do not, from the quest turn-in packet (CMSG_QUESTGIVER_CHOOSE_REWARD: before the core handles it
 * the quest must not be rewarded yet, after it it must be):
 *   - quest 2 (the student's cards, 919703 / 919713) lists the item 919610 "Hearthstone Starter Deck" as its reward
 *     (world migration 0011); right-clicking it awards the starter deck of the class of the character through
 *     HsAwardStarter (protected top-up every time, the deck once per account and class; owner decision, it used to
 *     be awarded at the turn-in). The 9 class deck items 919620 to 919628 unlock their class for the whole account
 *     and award its starter deck (the reward of the class-unlock questlines, later). Both are used up;
 *   - the turn-in of quest 3 (the lesson, 919704 / 919714) gives one Hearthstone Booster Pack item, once per account
 *     (character table hearthstone_account_intro, migration 0008). The lesson is a plain turn-in for now (owner,
 *     session 13); the scripted tutorial match comes later;
 *   - the turn-in of quest 5 (Byron's trade, 919706 / 919716) unlocks the crafting option of the crafting NPCs
 *     (HsCardMasterDone; owner decision).
 *
 * No creature script: a module CreatureScript on the Masters would break their vendor option (the core clears the
 * gossip menu for the script and then looks the selected option up in the empty menu) and hide their quest marker
 * (GetDialogStatus 0). Implemented in HsIntroGlue.cpp; HsMatchGlue.cpp calls the two packet functions.
 */

#ifndef MOD_HEARTHSTONE_HS_INTRO_GLUE_H
#define MOD_HEARTHSTONE_HS_INTRO_GLUE_H

#include <cstdint>

class Creature;
class GossipMenu;
class Player;
class WorldPacket;
class WorldSession;

// The quests of world migration 0008.
constexpr uint32_t kHsIntroCardsHorde = 919703;     // Too Many Rules (Brakgul to Ezra)
constexpr uint32_t kHsIntroLessonHorde = 919704;    // A Seat at the Table
constexpr uint32_t kHsIntroCardsAlliance = 919713;  // No Time to Play (Thomas Bell to Ibelin)
constexpr uint32_t kHsIntroLessonAlliance = 919714; // Your First Hand
constexpr uint32_t kHsIntroTradeHorde = 919706;     // The Card Master's Trade (Byron to Byron)
constexpr uint32_t kHsIntroTradeAlliance = 919716;  // the same quest for the Alliance

// The gossip tutorials (HsIntroGlue.cpp, kTutorials), told by an NPC while its quest is open (accepted, not complete):
//   - Ezra / Ibelin (919501 / 919502): the lesson, quest 3 (919704 / 919714). "Alright. How does Hearthstone work?" and
//     six more pages; the last option completes the quest (no tutorial match, owner decision);
//   - Byron (919505): his trade, quest 5 (919706 / 919716), "So what do you actually do?" and six more pages.
// The texts are npc_text rows (world migration 0010, generated from the script); the quests have SpecialFlags 2, so only
// the last gossip option completes them, and the turn-in is offered on the same page.
constexpr uint32_t kHsCardMasterEntry = 919505;      // Byron "Reckful" Bernstein (world migration 0009)

// Ezra and Ibelin also offer "Where can I learn to play other class decks?" once Byron's trade is turned in (HsCardMasterDone):
// a class list (npc_text 9195150 / 9195151) and one reply per class (npc_text 9195700-9195708 / 9195710-9195718, world
// migration 0018) that tells a short story and names the teacher and the place. The list is shown again under every reply.
// It is part of the tutorial gossip functions below.
//
// Does the creature tell a gossip tutorial (or offer the class deck hint) to the player now (its quest of the player's faction is open)?
bool HsIntroTutorialActive(Player* player, Creature* creature);
// Appends the first option of the creature's tutorial to menu (if it fits). The caller sends the menu.
void HsIntroAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender);
// Appends the class deck hint option to menu (if it fits). Only offered while a class is still missing. The caller
// sends the menu.
void HsIntroAppendClassHint(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender);
// A gossip select with the module's sender: true when action was a tutorial action (handled, the next page is sent).
bool HsIntroHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender);

// Item ids of world migration 0011 (deck items, soulbound, right-click, used up).
constexpr uint32_t kHsStarterDeckItem = 919610;   // the intro reward: the starter deck of the class of the user
constexpr uint32_t kHsClassDeckItemFirst = 919620; // Warrior, Paladin, Hunter, Rogue, Priest, Shaman, Mage, Warlock, Druid
constexpr uint32_t kHsClassDeckItemCount = 9;

// CMSG_OPEN_ITEM (right-click, before the core, not during a match): true when the item was a deck item and is consumed
// (used or refused), false for any other item.
bool HsIntroHandleOpenItem(Player* player, WorldPacket const& packet);

// CMSG_QUESTGIVER_CHOOSE_REWARD before the core: remembers an intro quest that is not rewarded yet.
void HsIntroBeforeTurnIn(WorldSession* session, WorldPacket const& packet);
// The same packet after the core handled it: the remembered quest is rewarded now -> the deck or the pack.
void HsIntroAfterTurnIn(WorldSession* session, WorldPacket const& packet);

// Has a character of the player's account turned in the card quest (919703 / 919713)? Unlocks the addon's micro menu
// button (WELCOME intro=). Cached per account: read once per login from character_queststatus of every character of
// the account (plus the player's own in-memory quest state), set at the turn-in.
bool HsIntroDone(Player* player);
// Has a character of the player's account turned in the Card Master's trade (919706 / 919716)? Only then do the
// crafting NPCs (Byron) offer "Craft and scrap cards". Cached per account like HsIntroDone, set at the turn-in.
bool HsCardMasterDone(Player* player);
// Has a character of the player's account completed the lesson quest (the last gossip option of Ezra / Ibelin, 919704 /
// 919714: completed or turned in)? Only then may the account start Hearthstone matches. Cached per account.
bool HsLessonDone(Player* player);
// Login / logout of a character of the account: forget the cached answers.
void HsIntroForget(uint32_t accountId);

// Implemented in HearthstoneScripts.cpp: INTRO_DONE to the account's handshaken character.
void HsPushIntroDone(uint32_t accountId);

#endif
