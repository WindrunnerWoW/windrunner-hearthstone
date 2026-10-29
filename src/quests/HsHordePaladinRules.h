#ifndef MOD_HEARTHSTONE_HS_HORDE_PALADIN_RULES_H
#define MOD_HEARTHSTONE_HS_HORDE_PALADIN_RULES_H

#include <array>
#include <cstdint>

constexpr uint32_t kHsHordePaladin = 919540;
constexpr uint32_t kHsHordePaladinResearch = 919801;
constexpr uint32_t kHsHordePaladinRehearsal = 919802;
constexpr uint32_t kHsHordePaladinDuel = 919803;
constexpr std::array<uint32_t, 4> kHsHordePaladinCredits = { 919541, 919542, 919543, 919544 };
constexpr uint32_t kHsHordePaladinActionFirst = 0x48530120u;
constexpr uint32_t kHsHordePaladinActionLast = kHsHordePaladinActionFirst + 11;

inline uint32_t HsHordePaladinNextStep(std::array<uint32_t, 2> const& counters)
{
    for (uint32_t step = 0; step < counters.size(); ++step)
        if (!counters[step]) return step;
    return 2;
}

inline bool HsHordePaladinLessonAllowed(bool horde, uint32_t level, bool introductionRewarded,
    bool questActive, bool questRewarded, bool prerequisiteRewarded)
{
    return horde && level >= 10 && introductionRewarded && questActive &&
        !questRewarded && prerequisiteRewarded;
}

inline bool HsHordePaladinActionAllowed(uint32_t step, uint32_t action, bool offered)
{
    return offered && step < 4 && action >= kHsHordePaladinActionFirst &&
        action <= kHsHordePaladinActionLast && (action - kHsHordePaladinActionFirst) / 3 == step;
}

// Transient demonstration time only. Native counters remain the saved progress.
struct HsHordePaladinSceneClock
{
    uint32_t elapsed = 0;
    bool Ready() const { return elapsed == 5000; }
    bool Advance(uint32_t diff)
    {
        elapsed += diff > 5000 - elapsed ? 5000 - elapsed : diff;
        return Ready();
    }
};

inline bool HsHordePaladinDemonstrationActionAllowed(uint32_t page, bool active, bool ready)
{
    return page < 2 ? !active : page == 2 && active && ready;
}

// A passage's pause starts at its actual emission. A stalled world tick advances
// at most one passage and discards excess time, preserving the following pause.
struct HsHordePaladinReadingClock
{
    uint32_t line = 0;
    uint32_t elapsed = 0;
    bool Done(uint32_t count) const { return line >= count; }
    bool Advance(uint32_t diff, uint32_t pause, uint32_t count)
    {
        if (Done(count)) return false;
        elapsed += diff > pause - elapsed ? pause - elapsed : diff;
        if (elapsed != pause) return false;
        elapsed = 0;
        ++line;
        return true;
    }
};

inline uint32_t HsHordePaladinSceneQuest(uint32_t objective)
{
    return objective < 2 ? kHsHordePaladinResearch :
        objective == 3 ? kHsHordePaladinRehearsal : 0;
}

inline bool HsHordePaladinReadingActionAllowed(uint32_t page, bool active)
{
    return page < 2 && !active;
}

// Native quest interactions stay blocked for the whole unfinished RP scene.
// Only its gossip/protest may open between the two timed sections.
inline bool HsHordePaladinInteractionAllowed(bool watchingThisNpc, bool readyForProtest, bool gossip)
{
    return !watchingThisNpc || (readyForProtest && gossip);
}

#endif
