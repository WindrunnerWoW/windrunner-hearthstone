/*
 * mod-hearthstone: world database glue of the World NPC Ladder. The row encodings (the column
 * order, the checks, the data structs) live in src/ladder/HsLadderWorld.* (pure C++); this file only reads rows.
 *
 * Tables (WORLD database, data/sql/world/0049_hearthstone_ladder_tables.sql): hearthstone_ladder_table (one row per
 * table of a host city), hearthstone_ladder_path (the points between spawn and stop), hearthstone_ladder_line (what an
 * NPC says after the match) and hearthstone_ladder_block (roster NPCs kept out of the offers). All four are owner
 * data: the server never writes them.
 */

#ifndef MOD_HEARTHSTONE_HS_LADDER_WORLD_STORE_H
#define MOD_HEARTHSTONE_HS_LADDER_WORLD_STORE_H

#include "ladder/HsLadderWorld.h"

#include <vector>

// Logs an error for every world table of the ladder that is missing; false if any is.
bool CheckHsLadderWorldTables();

// Reads the four tables into out (everything BuildWorld reports is logged) and checks every table against the
// gameobject table: the table, the player's chair (template Hs::Ladder::kLadderChairEntry) and the opponent's chair
// must exist on the table's map, else the table is logged and left out. False (logged) when a world table is missing
// or a row is malformed; true with no tables when the data holds none that works (logged).
bool LoadHsLadderWorld(Hs::Ladder::LadderWorld& out);

// The enabled rows of hearthstone_finder_npc whose creature_template has a script_name or an ai_name, by entry.
// False (logged) when a table is missing.
bool LoadHsLadderRosterRisks(std::vector<Hs::Ladder::RosterRisk>& out);

#endif
