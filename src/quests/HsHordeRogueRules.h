#ifndef MOD_HEARTHSTONE_HS_HORDE_ROGUE_RULES_H
#define MOD_HEARTHSTONE_HS_HORDE_ROGUE_RULES_H

#include <array>
#include <cstdint>

constexpr uint32_t kHsHordeRogue = 919522;
constexpr uint32_t kHsHordeRogueGora = 919523;
constexpr uint32_t kHsHordeRogueRikka = 919524;
constexpr uint32_t kHsHordeRogueZikko = 919525;
constexpr uint32_t kHsHordeRogueSable = 919526;
constexpr uint32_t kHsHordeRogueInvestigation = 919771;
constexpr uint32_t kHsHordeRogueRecovery = 919772;
constexpr uint32_t kHsHordeRogueDuel = 919773;
// Item objective, not a counter: a quest that mixes counters and items shows item progress twice.
constexpr uint32_t kHsHordeRogueEvidence = 919636;
constexpr uint32_t kHsHordeRogueInvestigationReport = 919637;
// Legacy objective item from the first recovery build. Never granted again; only destroyed from bags.
constexpr uint32_t kHsHordeRogueConfrontation = 919648;
constexpr std::array<uint32_t, 4> kHsHordeRogueContacts = {
    kHsHordeRogueRikka, kHsHordeRogueGora, kHsHordeRogueZikko, kHsHordeRogueSable };
constexpr std::array<uint32_t, 4> kHsHordeRogueCredits = { 919527, 919528, 919529, 919530 };

inline uint32_t HsHordeRogueStep(std::array<uint32_t, 4> const& counters)
{
    for (uint32_t i = 0; i < counters.size(); ++i)
        if (!counters[i])
            return i;
    return uint32_t(counters.size());
}

inline bool HsHordeRogueContactAllowed(bool horde, bool questIncomplete,
    std::array<uint32_t, 4> const& counters, uint32_t entry)
{
    uint32_t const step = HsHordeRogueStep(counters);
    return horde && questIncomplete && step < kHsHordeRogueContacts.size() &&
        kHsHordeRogueContacts[step] == entry;
}

inline bool HsHordeRogueReportAllowed(bool horde, bool questIncomplete,
    bool hasEvidenceInBags)
{
    return horde && questIncomplete && hasEvidenceInBags;
}

inline bool HsHordeRogueReturnAllowed(bool horde, bool investigationOpen,
    std::array<uint32_t, 4> const& counters, bool hasReport)
{
    return horde && investigationOpen && HsHordeRogueStep(counters) == 4 && !hasReport;
}

inline bool HsHordeRogueDirectionsAllowed(bool horde, bool investigationOpen,
    std::array<uint32_t, 4> const& counters, uint32_t contact)
{
    return horde && investigationOpen && contact < kHsHordeRogueContacts.size() &&
        HsHordeRogueStep(counters) == contact + 1;
}

// Sable's native vendor sells only the recovered deck, while the recovery quest is open.
// The daily card is a gossip purchase (HsCardTrader).
inline bool HsHordeRogueTradeAllowed(bool horde, bool investigationRewarded, bool recoveryIncomplete)
{
    return horde && investigationRewarded && recoveryIncomplete;
}

#endif
