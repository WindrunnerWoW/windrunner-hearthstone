#ifndef MOD_HEARTHSTONE_HS_ALLIANCE_PALADIN_RULES_H
#define MOD_HEARTHSTONE_HS_ALLIANCE_PALADIN_RULES_H

#include <array>
#include <cstdint>

constexpr uint32_t kHsSirRowan = 919570;
constexpr uint32_t kHsSquireTess = 919571;
constexpr uint32_t kHsAdaSparkplugz = 919577;
constexpr uint32_t kHsAdaCardCase = 919650;
constexpr uint32_t kHsAlliancePaladinReturn = 919851;
constexpr uint32_t kHsAlliancePaladinTable = 919852;
constexpr uint32_t kHsAlliancePaladinDuel = 919853;
constexpr std::array<uint32_t, 2> kHsAlliancePaladinCredits = {919575, 919576};
constexpr uint32_t kHsAlliancePaladinRetiredActionFirst = 0x485301c0u;
constexpr uint32_t kHsAlliancePaladinActionFirst = 0x485301ccu;
constexpr uint32_t kHsAlliancePaladinActionLast = kHsAlliancePaladinActionFirst + 7;

inline uint32_t HsAlliancePaladinNextStep(std::array<uint32_t, 3> const& counters, uint32_t count)
{
    if (count > counters.size()) return 3;
    for (uint32_t i = 0; i < count; ++i)
        if (!counters[i]) return i;
    return count;
}

inline bool HsAlliancePaladinLessonAllowed(bool alliance, uint32_t level, bool introRewarded,
    bool active, bool rewarded, bool previousRewarded)
{
    return alliance && level >= 10 && introRewarded && active && !rewarded && previousRewarded;
}

inline uint32_t HsAlliancePaladinPageCount(uint32_t exchange)
{
    return exchange == 0 ? 4 : exchange == 1 ? 2 : 0;
}

inline bool HsAlliancePaladinExchangeCreature(uint32_t exchange, uint32_t entry)
{
    return exchange == 0 ? entry == kHsSquireTess : exchange == 1 && entry == kHsSirRowan;
}

inline bool HsAlliancePaladinActionAllowed(uint32_t exchange, uint32_t action, bool offered)
{
    if (!offered || exchange >= kHsAlliancePaladinCredits.size() ||
        action < kHsAlliancePaladinActionFirst || action > kHsAlliancePaladinActionLast)
        return false;
    uint32_t const offset = action - kHsAlliancePaladinActionFirst;
    uint32_t const page = offset % 4;
    // The greeting already shows the first NPC line; delivery uses native item turn-in.
    return offset / 4 == exchange && page != 0 &&
        page < HsAlliancePaladinPageCount(exchange);
}

#endif
