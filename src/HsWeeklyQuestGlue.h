#ifndef MOD_HEARTHSTONE_HS_WEEKLY_QUEST_GLUE_H
#define MOD_HEARTHSTONE_HS_WEEKLY_QUEST_GLUE_H
#include "match/HsMatchService.h"
#include "quests/HsBgWeeklyQuests.h"
class Player;
void HsWeeklyRegisterRotations();
void HsWeeklyUpdate();
void HsWeeklyMatchStarted(Player* player, uint32_t matchId, bool randomHard = false,
    uint64_t durableId = 0, uint32_t matchNo = 0, bool practice = false, bool resume = false);
void HsWeeklyMatchEnded(Hs::MatchEnd const& end);
void HsWeeklyAfterQuestChange(Player* player, uint32_t acceptedQuest = 0);
void HsWeeklyLogin(Player* player);
void HsWeeklyForget(uint32_t guid);
// A finished Warsong Battlegrounds game: credits the character's open Battlegrounds weeklies (at the next update).
void HsWeeklyBgGameEnded(uint32_t guid, uint32_t account, Hs::BgWeeklyGame const& game);
#endif
