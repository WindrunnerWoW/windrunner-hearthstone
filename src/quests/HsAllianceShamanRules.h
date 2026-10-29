#ifndef MOD_HEARTHSTONE_HS_ALLIANCE_SHAMAN_RULES_H
#define MOD_HEARTHSTONE_HS_ALLIANCE_SHAMAN_RULES_H

#include <array>
#include <cstdint>

// Allocated after checking the current SQL and rule headers, including the
// uncommitted Horde Paladin chain. Models/names are supplied separately.
constexpr uint32_t kHsAllianceShaman = 919545;
constexpr uint32_t kHsAllianceShamanWildhammer = 919546;
constexpr uint32_t kHsAllianceShamanTauren = 919547;
constexpr uint32_t kHsAllianceShamanCredit = 919548;
constexpr uint32_t kHsAllianceShamanSupplies = 919811;
constexpr uint32_t kHsAllianceShamanReview = 919812;
constexpr uint32_t kHsAllianceShamanDuel = 919813;
constexpr uint32_t kHsAllianceShamanFittings = 919645;
constexpr uint32_t kHsAllianceShamanChair = 919059;
constexpr uint32_t kHsAllianceShamanBourbon = 2595;
constexpr uint32_t kHsAllianceShamanActionFirst = 0x48530140u;
constexpr uint32_t kHsAllianceShamanActionLast = kHsAllianceShamanActionFirst + 15;

// The single quest objective is the demonstration credit: 0 until the tour
// is acknowledged, then 4 (done).
inline uint32_t HsAllianceShamanNextStep(uint32_t demonstrationCounter)
{
    return demonstrationCounter ? 4 : 0;
}

inline bool HsAllianceShamanReviewAllowed(bool alliance, uint32_t level,
    bool introductionRewarded, bool suppliesRewarded, bool active, bool rewarded)
{
    return alliance && level >= 10 && introductionRewarded && suppliesRewarded && active && !rewarded;
}

inline bool HsAllianceShamanActionAllowed(uint32_t step, uint32_t action,
    bool offered, bool sceneActive, bool sceneReady)
{
    if (!offered || step >= 4 || action < kHsAllianceShamanActionFirst ||
        action > kHsAllianceShamanActionLast || (action - kHsAllianceShamanActionFirst) / 4 != step)
        return false;
    uint32_t const page = (action - kHsAllianceShamanActionFirst) % 4;
    // One readiness choice starts/retries the uninterrupted tour at the first
    // missing native stage. The sole acknowledgement is offered after return.
    return page == 3 ? sceneActive && sceneReady : !sceneActive && page == 0;
}

struct HsAllianceShamanStage
{
    uint32_t johnStop, visitorStop, visitorExit, spawnDelay;
    bool spawnAfterJohnArrival;
};
constexpr std::array<HsAllianceShamanStage, 4> kHsAllianceShamanStages = {{
    {5, 6, 9, 2000, false},
    {5, 6, 9, 2000, false},
    {6, 6, 10, 1500, false},
    {6, 6, 10, 1500, false},
}};
inline bool HsAllianceShamanSpawnAllowed(uint32_t step, bool johnAtStop)
{
    return step < kHsAllianceShamanStages.size() &&
        (!kHsAllianceShamanStages[step].spawnAfterJohnArrival || johnAtStop);
}

// John may demonstrate while a visitor approaches. A visitor's own exchange
// waits for both actors to reach their designated stops.
inline bool HsAllianceShamanSpeechAllowed(uint32_t step, bool johnAtStop,
    bool visitorExists, bool visitorAtStop)
{
    return step < 4 && johnAtStop && (!(step & 1u) || (visitorExists && visitorAtStop));
}
inline bool HsAllianceShamanArrived(bool splineFinished, float exact3dDistance)
{
    return splineFinished && exact3dDistance >= 0.0f && exact3dDistance <= 0.8f;
}
// Native smooth paths snap Z to the navmesh surface and add 0.5 yards.
// Bound that projection separately from arrival at the actual spline endpoint.
inline bool HsAllianceShamanPathEndpointAllowed(float horizontalDelta, float verticalDelta)
{
    return horizontalDelta >= 0.0f && horizontalDelta <= 1.0f &&
        verticalDelta >= 0.0f && verticalDelta <= 3.0f;
}
inline bool HsAllianceShamanActorAllowed(uint32_t entry, bool temporary)
{
    return temporary || (entry != kHsAllianceShamanWildhammer && entry != kHsAllianceShamanTauren);
}
// Transient route cursor shared with the native movement driver. Each real
// arrival advances one point; only the requested label pauses the actor.
struct HsAllianceShamanWalkState
{
    uint32_t next = 0, stop = 0, last = UINT32_MAX;
    bool moving = false, paused = false;
    uint32_t elapsed = 0;
    bool Arrive(uint32_t label, bool splineFinished, float nativeEndpointDistance)
    {
        if (!moving || !HsAllianceShamanArrived(splineFinished, nativeEndpointDistance)) return false;
        last = label; ++next; moving = false; elapsed = 0;
        paused = label == stop;
        return true;
    }
};

inline bool HsAllianceShamanReservedInteractionAllowed(bool owner, bool idle,
    bool readyOrAcknowledged, bool gossip)
{
    return owner && (idle || (readyOrAcknowledged && gossip));
}

// Transient RP playback only. Native quest counters are the saved progress.
// Each Advance follows a line already emitted by the world update. Discard
// excess elapsed time so a stalled update cannot collapse several RP passages.
struct HsAllianceShamanSceneClock
{
    uint32_t line = 0;
    uint32_t elapsed = 0;
    bool Ready(uint32_t count) const { return count != 0 && line >= count; }
    bool Advance(uint32_t diff, uint32_t pause, uint32_t count)
    {
        if (!count || Ready(count)) return false;
        elapsed += diff > pause - elapsed ? pause - elapsed : diff;
        if (elapsed != pause) return false;
        elapsed = 0;
        ++line;
        return true;
    }
};

#endif
