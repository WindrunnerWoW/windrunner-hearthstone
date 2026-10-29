#ifndef MOD_HEARTHSTONE_HS_ALLIANCE_ROGUE_RULES_H
#define MOD_HEARTHSTONE_HS_ALLIANCE_ROGUE_RULES_H
#include <array>
#include <cstdint>

constexpr uint32_t kHsAllianceRogueOperative = 919532;
constexpr uint32_t kHsAllianceRogue = 919533;
constexpr uint32_t kHsAllianceRogueAedis = 1478;
constexpr uint32_t kHsAllianceRogueOsric = 1323;
constexpr uint32_t kHsAllianceRogueInvestigation = 919781;
constexpr uint32_t kHsAllianceRogueCards = 919782;
constexpr uint32_t kHsAllianceRogueDuel = 919783;
constexpr uint32_t kHsAllianceRogueInvitation = 919638;
constexpr uint32_t kHsAllianceRogueDice = 919642;
constexpr uint32_t kHsAllianceRoguePickup = 919055;
constexpr std::array<uint32_t, 3> kHsAllianceRogueContacts = { 1478, 1323, 919533 };
constexpr std::array<uint32_t, 3> kHsAllianceRogueClues = { 919534, 919535, 919536 };
constexpr std::array<uint32_t, 3> kHsAllianceRogueCardItems = { 919639, 919640, 919641 };

inline uint32_t HsAllianceRogueClueStep(std::array<uint32_t, 3> const& counters)
{
    for (uint32_t i = 0; i < counters.size(); ++i)
        if (!counters[i]) return i;
    return 3;
}
inline bool HsAllianceRogueContactAllowed(bool alliance, bool incomplete,
    std::array<uint32_t, 3> const& counters, uint32_t entry)
{
    uint32_t const step = HsAllianceRogueClueStep(counters);
    return alliance && incomplete && step < 3 && kHsAllianceRogueContacts[step] == entry;
}
// Each card is an independent native objective. Possession includes the bank;
// an unclaimed or lost earlier card must never hide Osric or the upstairs pickup.
inline bool HsAllianceRogueCardAllowed(bool alliance, bool incomplete, bool hasThisCard)
{
    return alliance && incomplete && !hasThisCard;
}
inline bool HsAllianceRogueDiceAllowed(bool alliance, bool incomplete, bool hasDice)
{
    return alliance && incomplete && !hasDice;
}
inline bool HsAllianceRogueRiggedWin(bool distracted, bool hasDiceInBags)
{
    return distracted && hasDiceInBags;
}
inline bool HsAllianceRogueInvitationAllowed(bool alliance, uint32_t level,
    bool lessonRewarded, bool investigationAbsent, bool investigationRewarded, bool hasCard)
{
    return alliance && level >= 10 && lessonRewarded && investigationAbsent &&
        !investigationRewarded && !hasCard;
}
#endif
