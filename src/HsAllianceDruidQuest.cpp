#include "HsAllianceDruidQuest.h"
#include "quests/HsAllianceDruidRules.h"
#include "Creature.h"
#include "GossipDef.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "WorldSession.h"

#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr uint32 kReportPatience = 0x485300a2u;
    constexpr uint32 kAnswerFirst = 0x485300a3u;
    constexpr uint32 kAnswerLast = kAnswerFirst + 2;
    constexpr uint32 kReportGrowth = 0x485300a6u;
    constexpr uint32 kAnswerGrowth = 0x485300a7u;

    uint32 Step(Player* player, uint32 quest)
    {
        QuestStatusData const* status = player ? player->GetQuestStatusData(quest) : nullptr;
        return status ? HsAllianceDruidStep({ status->m_creatureOrGOcount[0],
            status->m_creatureOrGOcount[1] }) : 2;
    }

    bool Open(Player* player, uint32 quest, uint32 step)
    {
        return player && player->GetTeam() == ALLIANCE &&
            player->GetQuestStatus(quest) == QUEST_STATUS_INCOMPLETE && Step(player, quest) == step;
    }

    void AddOption(GossipMenu& menu, char const* label, uint32 sender, uint32 action)
    {
        menu.AddMenuItem(GOSSIP_ICON_CHAT, label, sender, action, "", false);
        menu.AddGossipMenuItemData(0, 0, 0);
    }

    void Exchange(Player* player, Creature* creature, uint32 text, char const* label,
        uint32 sender, uint32 action)
    {
        player->PlayerTalkClass->ClearMenus();
        AddOption(player->PlayerTalkClass->GetGossipMenu(), label, sender, action);
        player->PlayerTalkClass->SendGossipMenu(text, creature->GetObjectGuid());
    }

    void ReportDone(Player* player, Creature* creature, uint32 credit, uint32 text)
    {
        player->KilledMonsterCredit(credit);
        player->PlayerTalkClass->ClearMenus();
        player->PrepareQuestMenu(creature->GetObjectGuid());
        player->PlayerTalkClass->SendGossipMenu(text, creature->GetObjectGuid());
    }

    struct Request
    {
        uint32 guid;
        uint32 account;
    };
    struct Scene
    {
        Request request;
        HsAllianceDruidClock clock;
        bool started = false;
    };
    // Hooks enqueue; only the world update owns timers. No pointers survive a map removal or relog.
    std::mutex g_mutex;
    std::vector<Request> g_starts;
    std::vector<uint32> g_cancels;
    std::unordered_map<uint32, Scene> g_scenes;
    uint32 g_viewSweep = 0;

    bool Valid(Request const& request, Player* player)
    {
        return Open(player, kHsAllianceDruidPatience, 0) && player->IsInWorld() &&
            player->GetSession() && player->GetSession()->GetAccountId() == request.account;
    }
}

bool HsAllianceDruidGossipActive(Player* player, Creature* creature)
{
    return creature && creature->GetEntry() == kHsAllianceDruid &&
        (Open(player, kHsAllianceDruidPatience, 1) ||
         Open(player, kHsAllianceDruidGrowth, 1));
}

void HsAllianceDruidAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender)
{
    if (!HsAllianceDruidGossipActive(player, creature) || menu.MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS)
        return;
    if (Open(player, kHsAllianceDruidPatience, 1))
        AddOption(menu, "I have watched and waited.", sender, kReportPatience);
    else
        AddOption(menu, "I felt the wind and enjoyed the view from the tower.", sender, kReportGrowth);
}

bool HsAllianceDruidHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender)
{
    if (action < kReportPatience || action > kAnswerGrowth)
        return false;
    if (!player || !player->PlayerTalkClass)
        return true;
    if (!HsAllianceDruidGossipActive(player, creature) || !player->GetSession() || !player->IsAlive() ||
        !creature->IsAlive() || !player->IsWithinDistInMap(creature, INTERACTION_DISTANCE) ||
        !player->IsWithinLOSInMap(creature))
    {
        player->PlayerTalkClass->CloseGossip();
        return true;
    }
    if (Open(player, kHsAllianceDruidPatience, 1))
    {
        if (action == kReportPatience)
        {
            player->PlayerTalkClass->ClearMenus();
            GossipMenu& menu = player->PlayerTalkClass->GetGossipMenu();
            AddOption(menu, "Patience.", sender, kAnswerFirst);
            AddOption(menu, "Balance.", sender, kAnswerFirst + 1);
            AddOption(menu, "That this took quite a while.", sender, kAnswerLast);
            player->PlayerTalkClass->SendGossipMenu(9195172, creature->GetObjectGuid());
            return true;
        }
        if (action >= kAnswerFirst && action <= kAnswerLast)
        {
            ReportDone(player, creature, kHsAllianceDruidPatienceCredits[1], 9195173);
            return true;
        }
    }
    if (Open(player, kHsAllianceDruidGrowth, 1))
    {
        if (action == kReportGrowth)
        {
            Exchange(player, creature, 9195174, "That growth takes time. There is no need to rush.",
                sender, kAnswerGrowth);
            return true;
        }
        if (action == kAnswerGrowth)
        {
            ReportDone(player, creature, kHsAllianceDruidGrowthCredits[1], 9195175);
            return true;
        }
    }
    player->PlayerTalkClass->CloseGossip();
    return true;
}

void HsAllianceDruidQueue(Player* player)
{
    if (!Open(player, kHsAllianceDruidPatience, 0) || !player->GetSession())
        return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_starts.push_back({ player->GetGUIDLow(), player->GetSession()->GetAccountId() });
}

void HsAllianceDruidForget(uint32_t guid)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_cancels.push_back(guid);
}

void HsAllianceDruidUpdate(uint32_t diff)
{
    std::vector<Request> starts;
    std::vector<uint32> cancels;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        starts.swap(g_starts);
        cancels.swap(g_cancels);
    }
    for (uint32 guid : cancels)
        g_scenes.erase(guid);
    for (Request const& request : starts)
    {
        if (g_scenes.count(request.guid))
            continue; // Reopening gossip or repeated clicks cannot reset or fast-forward the wait.
        if (Valid(request, ObjectAccessor::FindPlayer(ObjectGuid(HIGHGUID_PLAYER, request.guid))))
            g_scenes.emplace(request.guid, Scene{ request, {}, false });
    }
    for (auto it = g_scenes.begin(); it != g_scenes.end();)
    {
        Scene& scene = it->second;
        Player* player = ObjectAccessor::FindPlayer(ObjectGuid(HIGHGUID_PLAYER, it->first));
        if (!Valid(scene.request, player))
        {
            it = g_scenes.erase(it);
            continue;
        }
        if (!player->IsAlive())
        {
            scene.clock = {};
            scene.started = false; // Resurrection automatically starts a fresh ten-second observation.
            ++it;
            continue;
        }
        bool const finished = scene.clock.Advance(scene.started ? diff : 0);
        scene.started = true; // The update before the scene began never counts toward its ten seconds.
        if (!finished)
        {
            ++it;
            continue;
        }
        player->KilledMonsterCredit(kHsAllianceDruidPatienceCredits[0]);
        it = g_scenes.erase(it);
    }

    g_viewSweep += diff;
    if (g_viewSweep < 250)
        return;
    g_viewSweep = 0;
    Map* map = sMapMgr.FindMap(1, 0);
    if (!map)
        return;
    for (auto const& reference : map->GetPlayers())
    {
        Player* player = reference.getSource();
        if (player && player->IsInWorld() && player->IsAlive() && player->GetSession() &&
            Open(player, kHsAllianceDruidGrowth, 0) &&
            HsAllianceDruidAtView(player->GetMapId(), player->GetInstanceId(), player->GetPositionX(),
                player->GetPositionY(), player->GetPositionZ()))
            player->KilledMonsterCredit(kHsAllianceDruidGrowthCredits[0]);
    }
}
