/*
 * mod-hearthstone: the core-facing glue of the Warsong Battlegrounds. This is the only file of
 * the Battlegrounds feature with core headers: the rules and the wire format are pure C++ in src/battlegrounds
 * (HsBgGame / HsBgSession / HsBgWire), this file connects them to the world.
 *
 * ENTRY. A player opens a game by talking to one of the
 * two Warsong Battlegrounds hosts (a Silverwing Sentinel for the Alliance, a Warsong Outrider for the Horde; world
 * migration 0059, gossip appended by HsBgHostAppendGossip) or, for a GM, with `.hs bg start`. Any player leaves with
 * `.hs bg leave`. The chat hook routes the addon's WRBG traffic here (a branch next to the WRHS path, which is
 * unchanged): the addon talks WRBG through its BgProtocol. The mode is turned on by the game shape (heroes, faction
 * pools, seat names) in StartGame.
 *
 * ONE GAME PER CHARACTER. A game is a Bg::Session (one human seat 0, the other seven the real HsBgAi) and lives until
 * `.hs bg leave`, logout or a map change. Its answers are pushed with the
 * WRBG prefix; the state (BG_STATE), the human's combat (BG_COMBAT) and the end result (BG_END) are in HsBgWire.h.
 *
 * THREADING. Every function here runs on the world thread (the chat hook and the world update) or the map thread (the
 * `.hs` command); the registry is guarded by ONE mutex (g_mutex in HsBgGlue.cpp) that is a LEAF: nothing that takes
 * another module lock (HbMatches, the arena glue, the session registry) is called while it is held, and frames are
 * sent after it is released. The lock order is therefore
 *   arena glue mutex -> (optional) [match service mutex] -> bg glue mutex,
 * which is why HsBgBusy is safe to call from the match and arena glue (they may hold their own lock).
 */

#ifndef MOD_HEARTHSTONE_HS_BG_GLUE_H
#define MOD_HEARTHSTONE_HS_BG_GLUE_H

#include <cstdint>
#include <string>
#include <vector>

class ChatHandler;
class Creature;
class GossipMenu;
class Player;

// Whether the mode is on (the .hs bg entry and WRBG traffic are accepted); off only when the effect registry failed
// to load.
bool HsBgEnabled();

// The chat prefix of the Battlegrounds traffic ("WRBG"); the chat hook routes messages with it to HsBgHandleFrame.
char const* HsBgPrefix();

// OnStartup, after the config load: loads the seat-name pool (hearthstone_finder_npc) and, with the switch on, logs the
// effect registry size and fails loudly unless it is 94.
void HsBgStartup();

// The character has a Battlegrounds game open (the busy rule of it blocks a card match, a ladder duel
// and an arena duel, and those block it). Safe to call while holding the match or arena glue lock.
bool HsBgBusy(uint32_t guidLow);

// One inbound WRBG message (the full chat message, "WRBG\t" prefix included), from the chat hook on the world thread.
// Strips the prefix, rate limits, reassembles and dispatches, then sends the answers with the WRBG prefix. Nothing is
// done while the switch is off.
void HsBgHandleFrame(Player* player, std::string const& wire);

// Cleanup: logout drops the game without a push; a map change ends it and pushes BG_CLOSE (the player is still in the
// world). Called by the player script.
void HsBgOnLogout(uint32_t guidLow);
void HsBgOnMapChanged(Player* player);
// World update: sweeps the inbound reassemblers and, when the switch was turned off at a reload, ends every open game.
void HsBgUpdate(uint32_t diff);

// `.hs bg start|leave|status` (args[0] == "bg"). leave and status are open to every player; start is GM-only (the
// hosts are the player entry). The caller routes "bg" before the SEC_MODERATOR gate of the other
// .hs subcommands.
void HsBgCommand(ChatHandler* handler, int security, std::vector<std::string> const& args);
void HsBgUsage(ChatHandler* handler);
// The "Battlegrounds:" line of `.hs status`.
void HsBgStatus(ChatHandler* handler);

// ---- the two host NPCs --------------------------------------------------------------------------------
// True when the creature entry is one of the two Warsong Battlegrounds hosts (Silverwing Sentinel / Warsong Outrider).
bool HsBgIsHost(uint32_t entry);
// True when the host serves this player's Warcraft faction, so the module offers its options. A host of the other
// faction only ever shows its greeting (the DB menu). Only offered while HsBgEnabled().
bool HsBgHostServes(Player* player, Creature* creature);
// Appends the host's options (play, how it works, and leave while a game is open) to the menu; the caller sends it.
void HsBgHostAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender);
// A gossip select with the module's sender: true when the action belonged to a Battlegrounds host (handled).
bool HsBgHostHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender);

#endif
