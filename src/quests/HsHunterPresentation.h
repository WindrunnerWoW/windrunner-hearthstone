#ifndef MOD_HEARTHSTONE_HS_HUNTER_PRESENTATION_H
#define MOD_HEARTHSTONE_HS_HUNTER_PRESENTATION_H

#include <array>
#include <cmath>
#include <cstdint>

struct HsHunterPoint { float x, y, z, facing; };
// Owner's replacement screenshots, 2026-10-09: map 1, Darnassus / Cenarion Enclave.
constexpr std::array<HsHunterPoint,3> kHsFennSpots = {{
    {10073.226562f,2565.486816f,1319.365234f,2.492005f},
    {10074.975586f,2568.148926f,1319.529907f,2.492005f},
    {10077.684570f,2571.716309f,1319.815796f,2.492005f},
}};
// Boro's WP1 is his native home position. Owner screenshots give WP2 and WP3.
constexpr std::array<HsHunterPoint,2> kHsBoroOuterSpots = {{
    {-1390.250366f,-139.095032f,158.360992f,2.164400f},
    {-1382.997314f,-135.163757f,156.927933f,2.167541f},
}};
constexpr uint32_t kHsHunterPresentationLimit = 60000;
constexpr uint32_t kHsHunterWalkLimit = 12000;
constexpr uint32_t kHsHunterNamePause = 1250;
constexpr uint32_t kHsHunterDescriptionPause = 5000;

inline uint32_t HsHunterSpot(bool alliance, uint32_t round, uint32_t animal)
{
    constexpr uint32_t order[2][3][3] = {
        {{0,1,2},{2,0,1},{1,0,2}}, // Boro: 1-2-3, 3-1-2, 2-1-3.
        {{0,1,2},{2,1,0},{0,1,2}}, // Fenn: forward, reverse, forward.
    };
    return round < 3 && animal < 3 ? order[alliance ? 1 : 0][round][animal] : 3;
}
inline bool HsHunterPathEndpointAllowed(float horizontal, float vertical)
{
    return std::isfinite(horizontal) && std::isfinite(vertical) && horizontal >= 0 && vertical >= 0 &&
        horizontal <= 0.8f && vertical <= 2.0f;
}
inline bool HsHunterAtPoint(bool splineFinished, float distance)
{
    return splineFinished && std::isfinite(distance) && distance >= 0 && distance <= 0.35f;
}
enum class HsHunterShowPhase { Walk, Name, Describe, Done };
// Transient performance timing only. Native quest counters save acknowledged rounds.
struct HsHunterShow
{
    uint32_t animal = 0, elapsed = 0;
    HsHunterShowPhase phase = HsHunterShowPhase::Walk;
    bool Arrive(bool splineFinished, float distance)
    {
        if (phase != HsHunterShowPhase::Walk || !HsHunterAtPoint(splineFinished,distance)) return false;
        phase = HsHunterShowPhase::Name; elapsed = 0; return true;
    }
    bool Advance(uint32_t diff)
    {
        if (phase != HsHunterShowPhase::Name && phase != HsHunterShowPhase::Describe) return false;
        uint32_t const pause = phase == HsHunterShowPhase::Name ? kHsHunterNamePause : kHsHunterDescriptionPause;
        elapsed += diff > pause-elapsed ? pause-elapsed : diff;
        if (elapsed != pause) return false;
        elapsed = 0;
        if (phase == HsHunterShowPhase::Name) phase = HsHunterShowPhase::Describe;
        else { ++animal; phase = animal == 3 ? HsHunterShowPhase::Done : HsHunterShowPhase::Walk; }
        return true; // Discard excess time; one late update never collapses several introductions.
    }
};
struct HsHunterIntroduction { char const* name; char const* description; };
constexpr HsHunterIntroduction kHsBoroIntroductions[3][3] = {
    // Boro describes imagined companions. He never acknowledges the player's ghost images.
    {{"Picture a bear.","A strong nose and stronger shoulders. It could carry half the camp and still have room for breakfast."},
     {"Or a buzzard.","Sharp eyes, broad wings. It could find a meal long before we noticed anything was missing."},
     {"Or a tiger.","Quiet feet and a patient eye. It would reach the quarry before you finished explaining the plan."}},
    {{"Now consider a wolf.","A keen tracker that stays with its pack. Very good company on a long walk."},
     {"Or a boar.","Stubborn, sturdy, and willing to put its nose into anything. It would never give up a trail."},
     {"Or a turtle.","Well protected and difficult to hurry. It would give you plenty of time to think."}},
    {{"Consider a frog.","Light on its feet and quick to leap. A small companion can still have a very large appetite."},
     {"Or a chicken.","An early riser. It would have the whole camp awake before the hunt began."},
     {"And a tauren accountant.","Patient, thorough, and very good at following a trail. Especially if someone owes him money."}},
};
constexpr HsHunterIntroduction kHsFennIntroductions[3][3] = {
    // Fenn only imagines these animals aloud while pointing at empty ground; he never mentions the images.
    {{"Suppose we brought her a cat.","Soft paws, a calm temper, and not a sound out of it."},
     {"Or an owl.","Silent wings and sharp eyes. It sees everything and says nothing."},
     {"Or a spider.","Eight legs, and not one of them makes a sound."}},
    {{"Now suppose a crab.","Armored, steady, and not one for chatter."},
     {"Or a crocolisk.","A big, patient animal. It will lie still for hours."},
     {"Or a rabbit.","Small, gentle and easy to keep. It eats very little."}},
    {{"Now, a rat.","Small, clever and quick. It takes up almost no room."},
     {"Or a sheep.","Soft, calm and perfectly peaceable. It never starts a fuss."},
     {"And last, a gnome accountant.","Tidy, punctual and silent. I would hire him myself."}},
};

#endif
