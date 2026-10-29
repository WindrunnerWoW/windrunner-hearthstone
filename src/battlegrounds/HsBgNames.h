/*
 * Battlegrounds seat names. The seven NPC seats of a game are named after real world
 * NPCs of the player's own faction (his three allies) and of the other faction (the four foes). The names come from
 * hearthstone_finder_npc, the ladder roster's own source, read once at startup into a Battlegrounds-owned copy (the
 * glue does that; this file is pure C++ and standard library only, like the rest of src/battlegrounds).
 *
 * The picking is a pure function of the pool, the human's faction and the game seed, so a game replays identically and
 * the selection is unit-tested headlessly (tests/bg_wire_tests.cpp). A pool too small for a side is topped up with that
 * side's plain generic names, so the result always holds exactly kPlayers-1 distinct names.
 */

#ifndef MOD_HEARTHSTONE_HS_BG_NAMES_H
#define MOD_HEARTHSTONE_HS_BG_NAMES_H

#include <cstdint>
#include <string>
#include <vector>

namespace Hs
{
    namespace Bg
    {
        // The faction tag of a name: the finder table's alliance / horde / any.
        enum class NameSide : uint8_t { Alliance, Horde, Any };

        struct NamePoolEntry
        {
            std::string name;
            NameSide side = NameSide::Any;
        };

        // The longest seat name the addon accepts (BgProtocol.lua). Longer names, and names with a '|' (the wire's
        // separator), are skipped.
        constexpr std::size_t kMaxSeatName = 64;

        // The kPlayers-1 = 7 seat names (seats 1..7; the human's own seat 0 stays empty). Seats 1-3 are the human's
        // allies (his own faction), 4-7 the foes; `any` names fill either side. All seven are distinct. Deterministic
        // in `seed` (the game seed).
        std::vector<std::string> PickSeatNames(std::vector<NamePoolEntry> const& pool, bool humanAlliance, uint32_t seed);

        // The plain generic names of a side, used only to fill the gaps a missing or too small finder table leaves.
        std::vector<std::string> const& GenericSeatNames(bool alliance);
    }
}

#endif
