#ifndef MOD_HEARTHSTONE_HS_ALLIANCE_PRIEST_RULES_H
#define MOD_HEARTHSTONE_HS_ALLIANCE_PRIEST_RULES_H

#include <array>
#include <cmath>
#include <cstdint>

constexpr uint32_t kHsAlenya = 919578;
constexpr uint32_t kHsSyrelle = 919579;
constexpr uint32_t kHsMaerithClasp = 919651;
constexpr uint32_t kHsAlliancePriestOrder = 919861;
constexpr uint32_t kHsAlliancePriestClasp = 919862;
constexpr uint32_t kHsAlliancePriestDuel = 919863;
constexpr std::array<uint32_t, 2> kHsAlliancePriestCredits = {919580, 919581};
constexpr uint32_t kHsAlliancePriestActionFirst = 0x485301e0u;
constexpr uint32_t kHsAlliancePriestActionLast = kHsAlliancePriestActionFirst + 15;
constexpr uint32_t kHsAlliancePriestRetry = kHsAlliancePriestActionFirst + 12;

inline uint32_t HsAlliancePriestNextStep(std::array<uint32_t, 2> const& counters)
{
    for (uint32_t i = 0; i < counters.size(); ++i)
        if (!counters[i]) return i;
    return static_cast<uint32_t>(counters.size());
}

inline bool HsAlliancePriestLessonAllowed(bool alliance, uint32_t level, bool introRewarded,
    bool active, bool rewarded, bool previousRewarded)
{
    return alliance && level >= 10 && introRewarded && active && !rewarded && previousRewarded;
}

inline uint32_t HsAlliancePriestPageCount(uint32_t exchange)
{
    return exchange == 0 || exchange == 2 ? 4 : exchange == 1 ? 2 : 0;
}

inline bool HsAlliancePriestExchangeCreature(uint32_t exchange, uint32_t entry)
{
    return exchange == 0 || exchange == 2 ? entry == kHsSyrelle : exchange == 1 && entry == kHsAlenya;
}

inline bool HsAlliancePriestActionAllowed(uint32_t exchange, uint32_t action, bool offered)
{
    if (!offered || exchange >= 3 || action < kHsAlliancePriestActionFirst ||
        action > kHsAlliancePriestActionLast) return false;
    if (action == kHsAlliancePriestRetry) return exchange == 2;
    uint32_t const offset = action - kHsAlliancePriestActionFirst;
    uint32_t const page = offset % 4;
    // Page zero is the greeting; only the reply offered on that page may advance it.
    return offset / 4 == exchange && page != 0 && page < HsAlliancePriestPageCount(exchange);
}

inline bool HsAlliancePriestClaspAllowed(bool lessonOpen, bool claspInBagsOrBank)
{
    return lessonOpen && !claspInBagsOrBank;
}

inline bool HsAlliancePriestClaspGrantAllowed(bool lessonOpen, bool claspInBagsOrBank,
    bool offeredFinalReply, bool canStore)
{
    return HsAlliancePriestClaspAllowed(lessonOpen, claspInBagsOrBank) && offeredFinalReply && canStore;
}

struct HsAlliancePriestScenePoint { float x, y, z, facing; };
// Owner's floor coordinates, 2026-10-09; GroundZ is not the temple floor.
constexpr HsAlliancePriestScenePoint kHsAlliancePriestSceneSpawn =
    {9652.713867f, 2554.165527f, 1333.463379f, 2.858773f};
constexpr HsAlliancePriestScenePoint kHsAlliancePriestSceneMeeting =
    {9628.962891f, 2565.766602f, 1335.039795f, 1.649260f};
constexpr uint32_t kHsAlliancePriestSceneMap = 1;
constexpr uint32_t kHsAlliancePriestSceneInstance = 0;
constexpr uint32_t kHsAlliancePriestSceneWalkLimit = 20000;
constexpr uint32_t kHsAlliancePriestSceneLifetime = 45000;
constexpr float kHsAlliancePriestSceneRange = 40.0f;
constexpr float kHsAlliancePriestSceneTeacherRange = 8.0f;

inline bool HsAlliancePriestRewardTransition(bool wasRewarded, bool nowRewarded)
{
    return !wasRewarded && nowRewarded;
}

inline bool HsAlliancePriestSceneStartAllowed(bool pending, bool wasRewarded, bool nowRewarded,
    bool eligible, bool interaction, bool alreadyRunning, uint32_t map, uint32_t instance)
{
    return pending && HsAlliancePriestRewardTransition(wasRewarded, nowRewarded) && eligible && interaction &&
        !alreadyRunning && map == kHsAlliancePriestSceneMap && instance == kHsAlliancePriestSceneInstance;
}

inline bool HsAlliancePriestSceneContinues(bool currentActor, bool identity, bool alive, bool peaceful,
    bool nearby, uint32_t map, uint32_t instance, uint32_t elapsed)
{
    return currentActor && identity && alive && peaceful && nearby && map == kHsAlliancePriestSceneMap &&
        instance == kHsAlliancePriestSceneInstance && elapsed < kHsAlliancePriestSceneLifetime;
}

inline bool HsAlliancePriestSceneVisible(uint32_t entry, bool sceneActor, bool ownActor)
{
    return entry != kHsSyrelle || !sceneActor || ownActor;
}

inline bool HsAlliancePriestSceneEndpointAllowed(float horizontal, float vertical)
{
    return std::isfinite(horizontal) && std::isfinite(vertical) && horizontal >= 0 && vertical >= 0 &&
        horizontal <= 0.8f && vertical <= 2.0f;
}

inline bool HsAlliancePriestSceneAtPoint(bool splineFinished, float distance)
{
    return splineFinished && std::isfinite(distance) && distance >= 0 && distance <= 0.35f;
}

enum class HsAlliancePriestScenePhase { Walk, Apology, Promise, Reply, Pause, Done };
// Transient visual timing only; this scene never saves or awards quest progress.
struct HsAlliancePriestSceneClock
{
    uint32_t elapsed = 0;
    HsAlliancePriestScenePhase phase = HsAlliancePriestScenePhase::Walk;

    bool Arrive(bool splineFinished, float distance)
    {
        if (phase != HsAlliancePriestScenePhase::Walk || !HsAlliancePriestSceneAtPoint(splineFinished, distance))
            return false;
        phase = HsAlliancePriestScenePhase::Apology;
        elapsed = 0;
        return true;
    }

    bool Advance(uint32_t diff)
    {
        if (phase == HsAlliancePriestScenePhase::Walk || phase == HsAlliancePriestScenePhase::Done) return false;
        uint32_t const pause = phase == HsAlliancePriestScenePhase::Apology ? 1250 :
            phase == HsAlliancePriestScenePhase::Pause ? 4000 : 5000;
        elapsed += diff > pause - elapsed ? pause - elapsed : diff;
        if (elapsed != pause) return false;
        elapsed = 0;
        phase = phase == HsAlliancePriestScenePhase::Apology ? HsAlliancePriestScenePhase::Promise :
            phase == HsAlliancePriestScenePhase::Promise ? HsAlliancePriestScenePhase::Reply :
            phase == HsAlliancePriestScenePhase::Reply ? HsAlliancePriestScenePhase::Pause :
            HsAlliancePriestScenePhase::Done;
        return true; // Discard excess time, so a late update never collapses the exchange.
    }
};

#endif
