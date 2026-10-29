#ifndef MOD_HEARTHSTONE_HS_HORDE_SHAMAN_RULES_H
#define MOD_HEARTHSTONE_HS_HORDE_SHAMAN_RULES_H

#include <array>
#include <cstdint>

constexpr uint32_t kHsAnnie = 919552;
constexpr uint32_t kHsAnnieSpirit = 919553;
constexpr uint32_t kHsAnnieGuidance = 919821;
constexpr uint32_t kHsAnnieFear = 919822;
constexpr uint32_t kHsAnnieSilence = 919823;
constexpr uint32_t kHsAnnieFarewell = 919824;
constexpr uint32_t kHsAnnieDuel = 919825;
constexpr uint32_t kHsAnnieWater = 159;
constexpr uint32_t kHsAnnieMailSender = 3030;
constexpr uint32_t kHsAnnieMailDelay = 3600;
constexpr uint32_t kHsAnnieDepartureLimit = 120000;
constexpr float kHsAnnieDepartureRange = 100.0f;
constexpr std::array<uint32_t, 3> kHsAnnieMentors = {3030, 3032, 3031};
constexpr std::array<uint32_t, 5> kHsAnnieCredits = {919554, 919555, 919556, 919557, 919558};
constexpr uint32_t kHsAnnieActionFirst = 0x48530180u;
constexpr uint32_t kHsAnnieActionLast = kHsAnnieActionFirst + 19;

inline uint32_t HsAnnieMentorIndex(uint32_t entry)
{
    for (uint32_t i = 0; i < kHsAnnieMentors.size(); ++i)
        if (kHsAnnieMentors[i] == entry) return i;
    return 3;
}

inline uint32_t HsAnnieMentorExchange(uint32_t entry, std::array<uint32_t, 3> const& counters)
{
    uint32_t const index = HsAnnieMentorIndex(entry);
    return index < counters.size() && !counters[index] ? index : 5;
}

inline bool HsAnnieVisible(uint32_t entry, bool farewellRewarded)
{
    return entry == kHsAnnie ? !farewellRewarded :
        entry == kHsAnnieSpirit ? farewellRewarded : true;
}

// A departure is a disposable visual after native turn-in, never saved progress.
inline bool HsAnnieDepartureVisible(uint32_t entry, bool rewarded, bool temporary,
    bool ownActor, bool departing)
{
    if (entry == kHsAnnie && temporary) return rewarded && ownActor && departing;
    if (entry == kHsAnnieSpirit) return rewarded && !departing;
    return HsAnnieVisible(entry, rewarded);
}

inline bool HsAnnieDepartureContinues(bool rewarded, bool ownerPresent, bool ownerAlive,
    bool actorAlive, bool withinRange, bool currentActor, bool arrived, uint32_t elapsed)
{
    return rewarded && ownerPresent && ownerAlive && actorAlive && withinRange &&
        currentActor && !arrived && elapsed < kHsAnnieDepartureLimit;
}

inline bool HsAnnieLessonAllowed(bool horde, uint32_t level, bool introRewarded,
    bool active, bool rewarded, bool prerequisiteRewarded)
{
    return horde && level >= 10 && introRewarded && active && !rewarded && prerequisiteRewarded;
}

inline bool HsAnnieRewardTransition(bool rewardedBefore, bool rewardedAfter)
{
    return !rewardedBefore && rewardedAfter;
}

// Pages are transient; only the final reply writes native objective credit.
inline uint32_t HsAnniePageCount(uint32_t exchange)
{
    return exchange < 3 ? 3 : exchange < 5 ? 4 : 0;
}

inline bool HsAnnieActionAllowed(uint32_t exchange, uint32_t action, bool offered)
{
    return offered && exchange < 5 && action >= kHsAnnieActionFirst && action <= kHsAnnieActionLast &&
        (action - kHsAnnieActionFirst) / 4 == exchange &&
        (action - kHsAnnieActionFirst) % 4 < HsAnniePageCount(exchange);
}

inline bool HsAnnieExchangeCreature(uint32_t exchange, uint32_t entry)
{
    return exchange < 3 ? entry == kHsAnnieMentors[exchange] :
        exchange < 5 && entry == kHsAnnie;
}

// Non-Shamans have no native training option. Each unfinished mentor still
// offers their own conversation, independently of the other two counters.
inline bool HsAnnieEmptyMentorGossipAllowed(uint32_t entry, bool guidanceIncomplete,
    std::array<uint32_t, 3> const& counters)
{
    return guidanceIncomplete && HsAnnieMentorExchange(entry, counters) < 3;
}

#endif
