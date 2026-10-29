#ifndef MOD_HEARTHSTONE_HS_HORDE_MAGE_RULES_H
#define MOD_HEARTHSTONE_HS_HORDE_MAGE_RULES_H

#include <array>
#include <cstdint>

constexpr uint32_t kHsHordeMage = 919510;
constexpr uint32_t kHsHordeMageExperiments = 919741;
constexpr uint32_t kHsHordeMageCombobulatorQuest = 919742;
constexpr uint32_t kHsHordeMageDuel = 919743;
constexpr uint32_t kHsHordeMageCombobulator = 919631;
constexpr std::array<uint32_t, 3> kHsHordeMageCredits = { 919511, 919512, 919513 };

// Native quest counters persist the next experiment across closing gossip and relogging.
inline uint32_t HsHordeMageNextExperiment(std::array<uint32_t, 3> const& counters)
{
    for (uint32_t step = 0; step < counters.size(); ++step)
        if (!counters[step])
            return step;
    return 3;
}

struct HsHordeMageClock
{
    uint32_t elapsed = 0;
    uint32_t next = 0;

    template<class Emit>
    bool Advance(uint32_t diff, Emit emit)
    {
        constexpr uint32_t duration = 2000;
        elapsed += diff > duration - elapsed ? duration - elapsed : diff;
        if (!next)
            emit(next++);
        return elapsed == duration;
    }
};

#endif
