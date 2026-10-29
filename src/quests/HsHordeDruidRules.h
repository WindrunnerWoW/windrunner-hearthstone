#ifndef MOD_HEARTHSTONE_HS_HORDE_DRUID_RULES_H
#define MOD_HEARTHSTONE_HS_HORDE_DRUID_RULES_H

#include <array>
#include <cstdint>

constexpr uint32_t kHsHordeDruid = 919514;
constexpr uint32_t kHsHordeDruidFeedCredit = 919515;
constexpr uint32_t kHsHordeDruidReportCredit = 919516;
constexpr uint32_t kHsHordeDruidFeedVendor = 3017;
constexpr uint32_t kHsHordeDruidFeedQuest = 919751;
constexpr uint32_t kHsHordeDruidCardsQuest = 919752;
constexpr uint32_t kHsHordeDruidDuel = 919753;
constexpr std::array<uint32_t, 4> kHsHordeDruidCardObjects = { 919051, 919052, 919053, 919054 };
constexpr std::array<uint32_t, 4> kHsHordeDruidCardItems = { 919632, 919633, 919634, 919635 };

// Saved native counters enforce Nan first, then Kolento; only both objectives permit native turn-in.
inline uint32_t HsHordeDruidFeedStep(std::array<uint32_t, 2> const& counters)
{
    for (uint32_t step = 0; step < counters.size(); ++step)
        if (!counters[step])
            return step;
    return 2;
}

struct HsHordeDruidCardPosition { float x, y, z, orientation; };
// Owner screenshots, 2026-10-04. Map 1, instance 0. Cards I/IV raised 0.25 after reported terrain clipping; Card I then lowered 0.1; Card IV moved to a new spot (ground Z 24.427891 + 0.25).
constexpr std::array<HsHordeDruidCardPosition, 4> kHsHordeDruidCardPositions = {{
    { -1027.704712f, -85.216057f, 59.867373f, 2.246871f },
    { -1074.357422f, -32.308929f, 141.276962f, 3.465810f },
    { -1075.561646f, -104.233727f, 151.004776f, 5.047601f },
    { -968.655945f, -179.621048f, 24.677891f, 3.480246f }
}};

inline uint32_t HsHordeDruidCardViewFlags(bool eligible)
{
    // Native quest activation; the installed LootSparkle effect requires chest type 3.
    return eligible ? 0x01u : 0x04u;
}

inline bool HsHordeDruidCardAllowed(bool horde, bool questIncomplete, bool hasThisCard)
{
    return horde && questIncomplete && !hasThisCard;
}

#endif
