#include "HsBgNames.h"

#include "HsBgGame.h"
#include "HsBgTypes.h"

#include <algorithm>
#include <set>

namespace Hs
{
    namespace Bg
    {
        namespace
        {
            constexpr int kOwnSeats = kFactionSize - 1; // 3 allies (seats 1-3; the human holds seat 0)
            constexpr int kFoeSeats = kFactionSize;     // 4 foes (seats 4-7)

            // Plain fallback names, only used when the finder table cannot cover a side.
            std::vector<std::string> const kAllianceGeneric = {
                "Silverwing Scout", "Silverwing Guard", "Silverwing Ranger", "Silverwing Archer",
                "Silverwing Warden", "Silverwing Priestess", "Silverwing Dreamer" };
            std::vector<std::string> const kHordeGeneric = {
                "Warsong Grunt", "Warsong Scout", "Warsong Raider", "Warsong Shaman",
                "Warsong Warrior", "Warsong Peon", "Warsong Wolfrider" };

            bool FitsSide(NameSide side, bool alliance)
            {
                if (side == NameSide::Any)
                    return true;
                return side == (alliance ? NameSide::Alliance : NameSide::Horde);
            }
        }

        std::vector<std::string> const& GenericSeatNames(bool alliance)
        {
            return alliance ? kAllianceGeneric : kHordeGeneric;
        }

        std::vector<std::string> PickSeatNames(std::vector<NamePoolEntry> const& pool, bool humanAlliance, uint32_t seed)
        {
            std::vector<std::string> own, other;
            for (NamePoolEntry const& entry : pool)
            {
                // The wire joins the names with '|' and the addon takes at most kMaxSeatName bytes per name.
                if (entry.name.empty() || entry.name.size() > kMaxSeatName || entry.name.find('|') != std::string::npos)
                    continue;
                // An `any` name may fill either side; using it on one side keeps it out of the other (see `used`).
                if (FitsSide(entry.side, humanAlliance))
                    own.push_back(entry.name);
                if (FitsSide(entry.side, !humanAlliance))
                    other.push_back(entry.name);
            }
            Rng rng(seed);
            rng.Shuffle(own);
            rng.Shuffle(other);

            std::vector<std::string> out(static_cast<std::size_t>(kOwnSeats + kFoeSeats));
            std::set<std::string> used;
            // Fills `count` free slots starting at `base` from `list`, skipping names already used; the number filled.
            auto fill = [&](std::vector<std::string> const& list, int base, int count)
            {
                int filled = 0;
                for (std::string const& name : list)
                {
                    if (filled >= count)
                        break;
                    if (used.insert(name).second)
                        out[static_cast<std::size_t>(base + filled++)] = name;
                }
                return filled;
            };

            int const ownGot = fill(own, 0, kOwnSeats);
            int const foeGot = fill(other, kOwnSeats, kFoeSeats);
            fill(GenericSeatNames(humanAlliance), ownGot, kOwnSeats - ownGot);
            fill(GenericSeatNames(!humanAlliance), kOwnSeats + foeGot, kFoeSeats - foeGot);
            return out;
        }
    }
}
