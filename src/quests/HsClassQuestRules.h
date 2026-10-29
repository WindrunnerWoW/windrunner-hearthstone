/* Pure rules shared by class quest glue and standalone tests. */
#ifndef MOD_HEARTHSTONE_HS_CLASS_QUEST_RULES_H
#define MOD_HEARTHSTONE_HS_CLASS_QUEST_RULES_H

#include "../match/HsMatchService.h"
#include "HsClassQuestOpponents.h"

constexpr uint32_t kHsWarriorVeteran = 919506;
constexpr uint32_t kHsWarriorForces = 919721;
constexpr uint32_t kHsWarriorProvisions = 919722;
constexpr uint32_t kHsWarriorWarGames = 919723;
constexpr uint32_t kHsHordeWarriorVeteran = 919507;
constexpr uint32_t kHsHordeThinkingGrunt = 919508;
constexpr uint32_t kHsHordeWaitingGrunt = 919509;
constexpr uint32_t kHsHordeWarriorWatch = 919731;
constexpr uint32_t kHsHordeWarriorDeckQuest = 919732;
constexpr uint32_t kHsHordeWarriorDuel = 919733;
constexpr uint32_t kHsHordeVeteranDeckItem = 919630;
constexpr uint32_t kHsHordeVeteranDeckObject = 919050;

inline bool HsClassQuestVictory(Hs::MatchEnd const& end)
{
    return Hs::ClassQuestOpponentOf(end.meta.opponentEntry) && end.res == "win" && end.why == "hero";
}
// This is a character's quest duel. An account roster unlock never enables direct rematches.
inline bool HsClassQuestDirectAllowed(Hs::ClassQuestOpponent const& opponent, bool alliance, bool questIncomplete)
{
    return opponent.alliance == alliance && questIncomplete;
}

inline bool HsWarriorQuestVictory(Hs::MatchEnd const& end)
{
    return end.meta.opponentEntry == kHsWarriorVeteran && end.res == "win" && end.why == "hero";
}

#endif
