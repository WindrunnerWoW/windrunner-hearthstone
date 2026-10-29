#ifndef MOD_HEARTHSTONE_HS_HUNTER_RULES_H
#define MOD_HEARTHSTONE_HS_HUNTER_RULES_H

#include <array>
#include <cstdint>

struct HsHunterChain
{
    uint32_t teacher, helper, firstQuest, intro, firstCredit, firstProjection, firstMenu, firstText, firstAction;
    bool alliance;
};
constexpr std::array<HsHunterChain, 2> kHsHunterChains = {{
    {919582,919583,919871,919704,919586,919900,65100,9195430,0x48530200u,false},
    {919584,919585,919881,919714,919591,919909,65160,9195490,0x48530240u,true},
}};
constexpr uint32_t kHsHunterGhostVisual = 9617;
constexpr uint32_t kHsHunterProjectionLifetime = 45000;
// How far the player may stray from the teacher while choosing without losing the round.
constexpr float kHsHunterAwayRange = 40.0f;

inline HsHunterChain const* HsHunterChainFor(uint32_t entry)
{
    for (auto const& chain : kHsHunterChains)
        if (entry == chain.teacher || entry == chain.helper) return &chain;
    return nullptr;
}
inline bool HsHunterProjection(uint32_t entry)
{
    for (auto const& chain : kHsHunterChains)
        if (entry >= chain.firstProjection && entry < chain.firstProjection + 9) return true;
    return false;
}
inline uint32_t HsHunterNextStep(std::array<uint32_t, 3> const& counters, uint32_t count)
{
    if (count > counters.size()) return 3;
    for (uint32_t i = 0; i < count; ++i) if (!counters[i]) return i;
    return count;
}
inline bool HsHunterLessonAllowed(bool alliance, HsHunterChain const& chain, uint32_t level,
    bool introRewarded, bool active, bool rewarded, bool previousRewarded)
{
    return alliance == chain.alliance && level >= 10 && introRewarded && active && !rewarded && previousRewarded;
}
enum class HsHunterPhase { Queued, Presenting, Choosing, AnswerQueued, Reaction, Expired, Cancelling };
enum class HsHunterAction { Invalid, Launch, Choose, Acknowledge, Talk };
struct HsHunterReply { HsHunterAction kind = HsHunterAction::Invalid; uint32_t step = 0, page = 0; };
inline HsHunterReply HsHunterDecode(HsHunterChain const& chain, uint32_t action)
{
    if (action < chain.firstAction || action >= chain.firstAction + 64) return {};
    uint32_t const code = action - chain.firstAction;
    if (code < 3) return {HsHunterAction::Launch, code, 0};
    if (code >= 8 && code < 17) return {HsHunterAction::Choose, (code-8)/3, (code-8)%3};
    if (code >= 20 && code < 23) return {HsHunterAction::Acknowledge, code-20, 0};
    if (code >= 32 && code < 35) return {HsHunterAction::Talk, 0, code-32};
    if (code >= 36 && code < 38) return {HsHunterAction::Talk, 1, code-36};
    return {};
}
inline bool HsHunterReplyAllowed(HsHunterReply reply, bool offered, bool teacher, bool audition,
    uint32_t nextStep, bool ownScene, HsHunterPhase phase)
{
    if (!offered || reply.kind == HsHunterAction::Invalid || reply.step != nextStep) return false;
    if (reply.kind == HsHunterAction::Talk)
        return !audition && nextStep < 2 && teacher == (nextStep == 1) && reply.page < (nextStep ? 2u : 3u);
    if (!teacher || !audition || nextStep >= 3) return false;
    if (reply.kind == HsHunterAction::Launch) return !ownScene;
    if (!ownScene) return false;
    return reply.kind == HsHunterAction::Choose ? phase == HsHunterPhase::Choosing :
        reply.kind == HsHunterAction::Acknowledge && phase == HsHunterPhase::Reaction;
}
inline bool HsHunterSceneContinues(bool currentToken, bool identity, bool interaction,
    bool questIncomplete, uint32_t nativeStep, uint32_t sceneStep)
{
    return currentToken && identity && interaction && questIncomplete && nativeStep == sceneStep && sceneStep < 3;
}
inline bool HsHunterVisualExpired(uint32_t elapsed) { return elapsed >= kHsHunterProjectionLifetime; }

#endif
