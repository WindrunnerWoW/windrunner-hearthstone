#ifndef MOD_HEARTHSTONE_HS_WARLOCK_RULES_H
#define MOD_HEARTHSTONE_HS_WARLOCK_RULES_H

#include <array>
#include <cstdint>

constexpr uint32_t kHsVarka = 919559;
constexpr uint32_t kHsSkrit = 919560;
constexpr uint32_t kHsMaren = 919561;
constexpr uint32_t kHsNib = 919562;
constexpr uint32_t kHsOwen = 919563;
constexpr uint32_t kHsWarlockScars = 919831;
constexpr uint32_t kHsWarlockCard = 919832;
constexpr uint32_t kHsVarkaDuel = 919833;
constexpr uint32_t kHsWarlockWitness = 919841;
constexpr uint32_t kHsWarlockFee = 919842;
constexpr uint32_t kHsMarenDuel = 919843;
constexpr uint32_t kHsWarlockPracticeCard = 919646;
constexpr uint32_t kHsWarlockAshes = 919647;
constexpr uint32_t kHsWarlockPort = 2593;
constexpr std::array<uint32_t, 7> kHsWarlockCredits = {919564, 919565, 919566, 0, 919567, 919568, 919569};
constexpr uint32_t kHsWarlockActionFirst = 0x485301a0u;
constexpr uint32_t kHsWarlockActionLast = kHsWarlockActionFirst + 27;

inline uint32_t HsWarlockNextStep(std::array<uint32_t, 3> const& counters, uint32_t count)
{
    if (count > counters.size()) return 3;
    for (uint32_t i = 0; i < count; ++i)
        if (!counters[i]) return i;
    return count;
}

inline bool HsWarlockLessonAllowed(bool correctFaction, uint32_t level, bool introRewarded,
    bool active, bool rewarded, bool previousRewarded)
{
    return correctFaction && level >= 10 && introRewarded && active && !rewarded && previousRewarded;
}

inline uint32_t HsWarlockPageCount(uint32_t exchange)
{
    return exchange < 3 || exchange == 5 ? 2 : exchange < 7 ? 4 : 0;
}

inline bool HsWarlockExchangeCreature(uint32_t exchange, uint32_t entry)
{
    return exchange < 3 ? entry == kHsVarka : exchange == 3 ? entry == kHsSkrit :
        exchange == 4 ? entry == kHsOwen : exchange == 5 ? entry == kHsMaren :
        exchange == 6 && entry == kHsNib;
}

inline bool HsWarlockActionAllowed(uint32_t exchange, uint32_t action, bool offered)
{
    return offered && exchange < 7 && action >= kHsWarlockActionFirst && action <= kHsWarlockActionLast &&
        (action - kHsWarlockActionFirst) / 4 == exchange &&
        (action - kHsWarlockActionFirst) % 4 < HsWarlockPageCount(exchange);
}

inline bool HsWarlockCardAllowed(bool lessonOpen, bool cardInBagsOrBank)
{
    return lessonOpen && !cardInBagsOrBank;
}

inline bool HsWarlockTradeAllowed(bool lessonOpen, bool cardInBagsOrBank, bool ashesInBags)
{
    return HsWarlockCardAllowed(lessonOpen, cardInBagsOrBank) && ashesInBags;
}

#endif
