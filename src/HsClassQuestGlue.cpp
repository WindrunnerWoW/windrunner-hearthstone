#include "HsClassQuestGlue.h"
#include "HsAllianceMageQuest.h"
#include "HsHunterQuest.h"
#include "HsWarlockQuest.h"
#include "quests/HsWarlockRules.h"
#include "HsHordeShamanQuest.h"
#include "quests/HsHordeShamanRules.h"
#include "HsAllianceDruidQuest.h"
#include "quests/HsAllianceDruidRules.h"
#include "HsHordeWarriorQuest.h"
#include "HsHordeMageQuest.h"
#include "quests/HsHordeMageRules.h"
#include "HsHordeDruidQuest.h"
#include "quests/HsHordeDruidRules.h"
#include "quests/HsHordePriestRules.h"
#include "HsHordeRogueQuest.h"
#include "quests/HsHordeRogueRules.h"
#include "HsAllianceShamanQuest.h"
#include "quests/HsAllianceShamanRules.h"
#include "HsHordePaladinQuest.h"
#include "quests/HsHordePaladinRules.h"
#include "HsAlliancePaladinQuest.h"
#include "quests/HsAlliancePaladinRules.h"
#include "HsAlliancePriestQuest.h"
#include "quests/HsAlliancePriestRules.h"
#include "HsAllianceRogueQuest.h"
#include "quests/HsAllianceRogueRules.h"
#include "Chat.h"
#include "Creature.h"
#include "GossipDef.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "WorldSession.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr uint32 kQuestionMe = 0x48530060u;
    constexpr uint32 kAnswerFirst = 0x48530061u;
    constexpr uint32 kQuestionCount = 5;
    constexpr uint32 kAnswersPerQuestion = 3;
    constexpr uint32 kAnswerLast = kAnswerFirst + kQuestionCount * kAnswersPerQuestion - 1;
    constexpr uint32 kInterestingText = 9195121;

    struct TacticalQuestion
    {
        uint32 text;
        char const* answers[kAnswersPerQuestion];
    };
    TacticalQuestion const kQuestions[kQuestionCount] = {
        { 9195120, { "Retreat.", "Reinforce.", "Hit him in the face." } },
        { 9195123, { "Dig in.", "Flank their position.", "Armor Up!" } },
        { 9195124, { "Hold the choke point.", "Draw them into open ground.", "Unleash the Hounds." } },
        { 9195125, { "Ration what remains.", "Send a runner for supplies.", "Topdeck lethal." } },
        { 9195126, { "Sound the retreat.", "Restore the battle lines.", "Brawl. Last minion standing." } },
    };

    void ShowQuestion(Player* player, Creature* creature, uint32 question, uint32 sender)
    {
        player->PlayerTalkClass->ClearMenus();
        GossipMenu& menu = player->PlayerTalkClass->GetGossipMenu();
        for (uint32 answer = 0; answer < kAnswersPerQuestion; ++answer)
        {
            menu.AddMenuItem(GOSSIP_ICON_CHAT, kQuestions[question].answers[answer], sender,
                kAnswerFirst + question * kAnswersPerQuestion + answer, "", false);
            menu.AddGossipMenuItemData(0, 0, 0);
        }
        player->PlayerTalkClass->SendGossipMenu(kQuestions[question].text, creature->GetObjectGuid());
    }

    bool ForcesOpen(Player* player, Creature* creature)
    {
        return player && creature && player->GetTeam() == ALLIANCE &&
            creature->GetEntry() == kHsWarriorVeteran &&
            player->GetQuestStatus(kHsWarriorForces) == QUEST_STATUS_INCOMPLETE;
    }

    struct Battle
    {
        uint32 matchId = 0;
        uint32 accountId = 0;
        uint32 mapId = 0;
        uint64 creature = 0;
        bool questOpen = false;
        uint32 entry = 0;
        uint32 questId = 0;
        bool alliance = true;
    };
    struct Notice
    {
        uint32 guid = 0;
        Battle battle;
        bool victory = false;
    };
    // Leaf lock: never call a core or match-service API while holding it.
    std::mutex g_mutex;
    std::unordered_map<uint32, Battle> g_battles;
    std::vector<Notice> g_notices;
}

bool HsClassQuestGossipActive(Player* player, Creature* creature)
{
    return HsAllianceMageGossipActive(player, creature) || HsHunterGossipActive(player, creature) || HsWarlockGossipActive(player, creature) || HsHordeShamanGossipActive(player, creature) || ForcesOpen(player, creature) || HsHordeWarriorSceneOpen(player, creature) ||
        HsHordeMageGossipActive(player, creature) || HsHordeDruidGossipActive(player, creature) ||
        HsAllianceDruidGossipActive(player, creature) || HsHordeRogueGossipActive(player, creature) ||
        HsAllianceRogueGossipActive(player, creature) || HsHordePaladinGossipActive(player, creature) ||
        HsAlliancePaladinGossipActive(player, creature) || HsAlliancePriestGossipActive(player, creature) ||
        HsAllianceShamanGossipActive(player, creature);
}

void HsClassQuestAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender)
{
    HsAllianceMageAppendGossip(player, creature, menu, sender);
    HsHunterAppendGossip(player, creature, menu, sender);
    HsWarlockAppendGossip(player, creature, menu, sender);
    HsHordeShamanAppendGossip(player, creature, menu, sender);
    HsHordeWarriorAppendGossip(player, creature, menu, sender);
    HsHordeMageAppendGossip(player, creature, menu, sender);
    HsHordeDruidAppendGossip(player, creature, menu, sender);
    HsAllianceDruidAppendGossip(player, creature, menu, sender);
    HsHordeRogueAppendGossip(player, creature, menu, sender);
    HsAllianceRogueAppendGossip(player, creature, menu, sender);
    HsHordePaladinAppendGossip(player, creature, menu, sender);
    HsAlliancePaladinAppendGossip(player, creature, menu, sender);
    HsAlliancePriestAppendGossip(player, creature, menu, sender);
    HsAllianceShamanAppendGossip(player, creature, menu, sender);
    if (!ForcesOpen(player, creature) || menu.MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS)
        return;
    menu.AddMenuItem(GOSSIP_ICON_CHAT, "Question me.", sender, kQuestionMe, "", false);
    menu.AddGossipMenuItemData(0, 0, 0);
}

bool HsClassQuestHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender)
{
    if (HsAllianceMageHandleSelect(player, creature, action, sender))
        return true;
    if (HsHunterHandleSelect(player, creature, action, sender))
        return true;
    if (HsWarlockHandleSelect(player, creature, action, sender))
        return true;
    if (HsHordeShamanHandleSelect(player, creature, action, sender))
        return true;
    if (HsHordeWarriorHandleSelect(player, creature, action))
        return true;
    if (HsHordeMageHandleSelect(player, creature, action, sender))
        return true;
    if (HsHordeDruidHandleSelect(player, creature, action, sender))
        return true;
    if (HsAllianceDruidHandleSelect(player, creature, action, sender))
        return true;
    if (HsHordeRogueHandleSelect(player, creature, action, sender))
        return true;
    if (HsAllianceRogueHandleSelect(player, creature, action, sender))
        return true;
    if (HsAllianceShamanHandleSelect(player, creature, action, sender))
        return true;
    if (HsHordePaladinHandleSelect(player, creature, action, sender))
        return true;
    if (HsAlliancePaladinHandleSelect(player, creature, action, sender))
        return true;
    if (HsAlliancePriestHandleSelect(player, creature, action, sender))
        return true;
    if (action != kQuestionMe && (action < kAnswerFirst || action > kAnswerLast))
        return false;
    if (!player || !player->PlayerTalkClass)
        return true;
    if (!ForcesOpen(player, creature))
    {
        player->PlayerTalkClass->CloseGossip();
        return true;
    }
    if (action == kQuestionMe)
    {
        ShowQuestion(player, creature, 0, sender);
        return true;
    }
    creature->MonsterSayToPlayer("Interesting.", player);
    uint32 const question = (action - kAnswerFirst) / kAnswersPerQuestion;
    if (question + 1 < kQuestionCount)
    {
        ShowQuestion(player, creature, question + 1, sender);
        return true;
    }
    player->AreaExploredOrEventHappens(kHsWarriorForces);
    player->PlayerTalkClass->ClearMenus();
    player->PrepareQuestMenu(creature->GetObjectGuid());
    player->PlayerTalkClass->SendGossipMenu(kInterestingText, creature->GetObjectGuid());
    return true;
}

bool HsClassQuestOpponentAvailable(Player* player, uint32_t entry)
{
    Hs::ClassQuestOpponent const* opponent = Hs::ClassQuestOpponentOf(entry);
    if (!opponent)
        return true;
    return player && HsClassQuestDirectAllowed(*opponent, player->GetTeam() == ALLIANCE,
        player->GetQuestStatus(opponent->questId) == QUEST_STATUS_INCOMPLETE);
}

void HsClassQuestMatchStarted(Player* player, Creature* creature, uint32_t matchId)
{
    if (!player || !player->GetSession() || !creature)
        return;
    Hs::ClassQuestOpponent const* opponent = Hs::ClassQuestOpponentOf(creature->GetEntry());
    if (!opponent)
        return;
    Battle battle;
    battle.matchId = matchId;
    battle.accountId = player->GetSession()->GetAccountId();
    battle.mapId = player->GetMapId();
    battle.creature = creature->GetObjectGuid().GetRawValue();
    battle.entry = opponent->entry;
    battle.questId = opponent->questId;
    battle.alliance = opponent->alliance;
    battle.questOpen = player->GetQuestStatus(opponent->questId) == QUEST_STATUS_INCOMPLETE;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_battles[player->GetGUIDLow()] = battle;
}

void HsClassQuestMatchEnded(Hs::MatchEnd const& end)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    auto const it = g_battles.find(end.guid);
    if (it == g_battles.end() || it->second.matchId != end.matchId)
        return;
    if (HsClassQuestVictory(end) && it->second.entry == end.meta.opponentEntry &&
        it->second.accountId == end.meta.accountId)
        g_notices.push_back({ end.guid, it->second, true });
    g_battles.erase(it);
}

void HsClassQuestUpdate()
{
    std::vector<Notice> notices;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        notices.swap(g_notices);
    }
    for (Notice const& notice : notices)
    {
        Player* player = ObjectAccessor::FindPlayer(ObjectGuid(HIGHGUID_PLAYER, notice.guid));
        if (!player || !player->IsInWorld() || !player->GetSession() ||
            player->GetSession()->GetAccountId() != notice.battle.accountId ||
            (player->GetTeam() == ALLIANCE) != notice.battle.alliance)
            continue;
        Hs::ClassQuestOpponent const* opponent = Hs::ClassQuestOpponentOf(notice.battle.entry);
        // Credit belongs to a quest accepted before this duel. A win in another duel never counts.
        if (notice.victory && notice.battle.questOpen &&
            player->GetQuestStatus(notice.battle.questId) == QUEST_STATUS_INCOMPLETE)
        {
            player->AreaExploredOrEventHappens(notice.battle.questId);
            if (opponent && opponent->duelComplete)
                ChatHandler(player->GetSession()).PSendSysMessage("Hearthstone: %s", opponent->duelComplete);
        }
        Creature* creature = player->GetMapId() == notice.battle.mapId
            ? player->GetMap()->GetCreature(ObjectGuid(notice.battle.creature)) : nullptr;
        if (!creature || !creature->IsAlive())
            continue;
        if (notice.victory && opponent)
            for (char const* line : opponent->victoryLines)
                if (line)
                    creature->MonsterSayToPlayer(line, player);
    }
}

void HsClassQuestInvalidateQuest(uint32_t guid, uint32_t quest)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    auto const battle = g_battles.find(guid);
    if (battle != g_battles.end() && battle->second.questId == quest)
        battle->second.questOpen = false;
    for (Notice& notice : g_notices)
        if (notice.guid == guid && notice.battle.questId == quest)
            notice.battle.questOpen = false;
}

void HsClassQuestAfterQuestChange(Player* player)
{
    if (!player)
        return;
    uint32 const guid = player->GetGUIDLow();
    std::vector<uint32> quests;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto const battle = g_battles.find(guid);
        if (battle != g_battles.end())
            quests.push_back(battle->second.questId);
        for (Notice const& notice : g_notices)
            if (notice.guid == guid)
                quests.push_back(notice.battle.questId);
    }
    // Status is read outside the leaf lock. Reaccepting later starts a fresh quest that needs a fresh duel.
    for (uint32 quest : quests)
        if (player->GetQuestStatus(quest) == QUEST_STATUS_NONE)
            HsClassQuestInvalidateQuest(guid, quest);
}

void HsClassQuestForget(uint32_t guid)
{
    HsHunterForget(guid);
    HsHordeShamanForget(guid);
    HsHordeWarriorForget(guid);
    HsHordeMageForget(guid);
    HsHordePaladinForget(guid);
    HsAllianceShamanForget(guid);
    HsAllianceDruidForget(guid);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_battles.erase(guid);
    g_notices.erase(std::remove_if(g_notices.begin(), g_notices.end(),
        [guid](Notice const& notice) { return notice.guid == guid; }), g_notices.end());
}
