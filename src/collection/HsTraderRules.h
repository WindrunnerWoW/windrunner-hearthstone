#ifndef MOD_HEARTHSTONE_TRADER_RULES_H
#define MOD_HEARTHSTONE_TRADER_RULES_H

#include "HsOwnership.h"
#include <set>
#include <sstream>

namespace Hs
{
    inline bool ParseTraderSwapTime(std::string const& value, uint32_t& seconds)
    {
        if (value.size() != 5 || value[2] != ':' || value[0] < '0' || value[0] > '9' ||
            value[1] < '0' || value[1] > '9' || value[3] < '0' || value[3] > '9' ||
            value[4] < '0' || value[4] > '9') return false;
        uint32_t const hour = (value[0] - '0') * 10 + value[1] - '0';
        uint32_t const minute = (value[3] - '0') * 10 + value[4] - '0';
        if (hour > 23 || minute > 59) return false;
        seconds = hour * 3600 + minute * 60;
        return true;
    }

    // UTC boundary timestamp, including the previous day's boundary before swap time.
    inline uint64_t TraderPeriod(uint64_t now, uint32_t swapSeconds)
    {
        return now < swapSeconds ? 0 : (now - swapSeconds) / 86400 * 86400 + swapSeconds;
    }

    // Index into the sorted eligible pool for a period (splitmix64: stable across restarts).
    inline std::size_t TraderPick(std::size_t poolSize, uint64_t period)
    {
        uint64_t z = period + 0x9E3779B97F4A7C15ULL;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return static_cast<std::size_t>((z ^ (z >> 31)) % poolSize);
    }

    inline std::set<std::string> TraderExclusions(std::string value, CardCatalog const& catalog)
    {
        for (char& ch : value) if (ch == ',') ch = ' ';
        std::istringstream input(value);
        std::set<std::string> excluded;
        std::string id;
        while (input >> id)
        {
            std::string const canonical = catalog.Resolve(id);
            // Preserve unknown ids too: operators can reserve cards before their dataset arrives.
            excluded.insert(canonical.empty() ? id : canonical);
        }
        return excluded;
    }

    inline bool TraderEligible(std::string const& id, CardRule const& rule, std::set<std::string> const& excluded)
    {
        // Standard only: the daily card trader never offers reward_only or quest (Naxxramas) cards.
        return rule.collectible && rule.acquisition == Acquisition::Standard &&
            rule.rarity != Rarity::None && excluded.count(id) == 0;
    }

    inline uint32_t TraderPrice(Rarity rarity)
    {
        switch (rarity)
        {
            case Rarity::Free: case Rarity::Common: return 50000;
            case Rarity::Rare: return 250000;
            case Rarity::Epic: return 500000;
            case Rarity::Legendary: return 2000000;
            default: return 0;
        }
    }
}
#endif
