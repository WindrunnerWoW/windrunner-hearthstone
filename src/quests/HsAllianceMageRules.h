#ifndef MOD_HEARTHSTONE_HS_ALLIANCE_MAGE_RULES_H
#define MOD_HEARTHSTONE_HS_ALLIANCE_MAGE_RULES_H

#include <array>
#include <cstdint>

constexpr uint32_t kHsMitchellJones = 919596;
constexpr std::array<uint32_t, 3> kHsAllianceMageImages = {{919597, 919598, 919599}};
constexpr uint32_t kHsAllianceMageQuiz = 919891;
constexpr uint32_t kHsAllianceMageTour = 919892;
constexpr uint32_t kHsAllianceMageDuel = 919893;
constexpr uint32_t kHsAllianceMageIntro = 919714;
constexpr uint32_t kHsAllianceMageQuizCredit = 919600;
constexpr std::array<uint32_t, 3> kHsAllianceMageTourCredits = {{919601, 919602, 919603}};
constexpr uint32_t kHsAllianceMageQuestionCount = 10;
constexpr uint32_t kHsAllianceMageFirstMenu = 65230;
constexpr uint32_t kHsAllianceMageFirstText = 9195600;
constexpr uint32_t kHsAllianceMageActionFirst = 0x48530280u;
constexpr uint32_t kHsAllianceMageActionLast = kHsAllianceMageActionFirst + 127;

inline uint32_t HsAllianceMageImageIndex(uint32_t entry)
{
    for (uint32_t index = 0; index < kHsAllianceMageImages.size(); ++index)
        if (entry == kHsAllianceMageImages[index]) return index;
    return static_cast<uint32_t>(kHsAllianceMageImages.size());
}

inline bool HsAllianceMageLessonAllowed(bool alliance, uint32_t level, bool introRewarded,
    bool active, bool rewarded, bool previousRewarded)
{
    return alliance && level >= 10 && introRewarded && active && !rewarded && previousRewarded;
}

inline bool HsAllianceMageImageVisible(bool alliance, uint32_t level, bool introRewarded,
    bool active, bool rewarded, bool previousRewarded)
{
    return HsAllianceMageLessonAllowed(alliance, level, introRewarded, active, rewarded, previousRewarded);
}

inline uint32_t HsAllianceMageQuizNextQuestion(uint32_t savedCount)
{
    return savedCount < kHsAllianceMageQuestionCount ? savedCount : kHsAllianceMageQuestionCount;
}

enum class HsAllianceMageAction { Invalid, Launch, Answer, Acknowledge, Tour, Afterward };
struct HsAllianceMageReply
{
    HsAllianceMageAction kind = HsAllianceMageAction::Invalid;
    uint32_t step = 0;
    uint32_t page = 0;
};

inline HsAllianceMageReply HsAllianceMageDecode(uint32_t action)
{
    if (action < kHsAllianceMageActionFirst || action > kHsAllianceMageActionLast) return {};
    uint32_t const code = action - kHsAllianceMageActionFirst;
    if (code < 10) return {HsAllianceMageAction::Launch, code, 0};
    if (code >= 16 && code < 46) return {HsAllianceMageAction::Answer, (code - 16) / 3, (code - 16) % 3};
    if (code >= 48 && code < 58) return {HsAllianceMageAction::Acknowledge, code - 48, 0};
    if (code >= 64 && code < 76 && (code - 64) % 4 < 3)
        return {HsAllianceMageAction::Tour, (code - 64) / 4, (code - 64) % 4};
    if (code == 80) return {HsAllianceMageAction::Afterward, 0, 0};
    return {};
}

inline bool HsAllianceMageQuizReplyAllowed(HsAllianceMageReply reply, bool offered,
    bool teacher, bool questIncomplete, uint32_t nativeQuestion)
{
    if (!offered || !teacher || !questIncomplete || nativeQuestion >= kHsAllianceMageQuestionCount ||
        reply.step != nativeQuestion) return false;
    if (reply.kind == HsAllianceMageAction::Answer) return reply.page < 3;
    return (reply.kind == HsAllianceMageAction::Launch || reply.kind == HsAllianceMageAction::Acknowledge) &&
        reply.page == 0;
}

inline bool HsAllianceMageTourReplyAllowed(HsAllianceMageReply reply, bool offered,
    uint32_t imageIndex, bool questIncomplete, bool visitDone)
{
    return offered && questIncomplete && !visitDone && imageIndex < kHsAllianceMageImages.size() &&
        reply.kind == HsAllianceMageAction::Tour && reply.step == imageIndex && reply.page < 3;
}

#endif
