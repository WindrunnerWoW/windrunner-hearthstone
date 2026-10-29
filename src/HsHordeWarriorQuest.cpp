#include "HsHordeWarriorQuest.h"
#include "quests/HsClassQuestRules.h"
#include "quests/HsHordeWarriorRules.h"
#include "Chat.h"
#include "Creature.h"
#include "GameObject.h"
#include "HsQuestPickup.h"
#include "GossipDef.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "UpdateData.h"
#include "UpdateMask.h"
#include "WorldSession.h"

#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr uint32 kWatchAction = 0x48530070u;

    struct SceneRequest
    {
        uint32 guid = 0;
        uint32 account = 0;
        ObjectGuid teacher;
    };
    struct Scene
    {
        SceneRequest request;
        ObjectGuid thinking;
        ObjectGuid waiting;
        HsHordeSceneClock clock;
    };
    // Gossip/logout hooks enqueue only. Scene state belongs to the world update.
    std::mutex g_queueMutex;
    std::vector<SceneRequest> g_starts;
    std::vector<uint32> g_cancels;
    std::unordered_map<uint32, Scene> g_scenes;

    bool NeedsDeck(Player const* player)
    {
        return player && player->IsInWorld() && HsHordePickupAllowed(player->GetTeam() == HORDE,
            player->GetQuestStatus(kHsHordeWarriorDeckQuest) == QUEST_STATUS_INCOMPLETE,
            player->GetItemCount(kHsHordeVeteranDeckItem, true) != 0);
    }

    class DeckPickupInteraction final : public HsQuestPickupAI
    {
    public:
        explicit DeckPickupInteraction(GameObject* go) : HsQuestPickupAI(go, kHsHordeVeteranDeckItem) {}
    protected:
        bool IsEligible(Player const* player) const override { return NeedsDeck(player); }
        void OnCollected(Player* player) override
        {
            ChatHandler(player->GetSession()).SendSysMessage("Several cards have circular ale stains. "
                "Return the deck to the veteran.");
        }
    };

    class HordeDeckPickupScript final : public AllGameObjectScript
    {
    public:
        HordeDeckPickupScript() : AllGameObjectScript("hearthstone_horde_warrior_deck_pickup") {}
        GameObjectAI* GetGameObjectAI(GameObject* go) const override
        {
            return go->GetEntry() == kHsHordeVeteranDeckObject ? new DeckPickupInteraction(go) : nullptr;
        }
    };

    ObjectGuid FindActor(Map* map, Creature* teacher, uint32 entry)
    {
        Creature* nearest = nullptr;
        for (auto const& row : map->GetCreatureBySpawnIdStore())
        {
            Creature* creature = row.second;
            if (creature->GetEntry() == entry && creature->IsAlive() &&
                teacher->IsWithinDistInMap(creature, 20.0f) &&
                (!nearest || teacher->GetDistance(creature) < teacher->GetDistance(nearest)))
                nearest = creature;
        }
        return nearest ? nearest->GetObjectGuid() : ObjectGuid();
    }

}

bool HsHordeWarriorSceneOpen(Player* player, Creature* creature)
{
    return player && creature && player->GetTeam() == HORDE &&
        creature->GetEntry() == kHsHordeWarriorVeteran &&
        player->GetQuestStatus(kHsHordeWarriorWatch) == QUEST_STATUS_INCOMPLETE;
}

void HsHordeWarriorAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender)
{
    if (!HsHordeWarriorSceneOpen(player, creature) || menu.MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS)
        return;
    menu.AddMenuItem(GOSSIP_ICON_CHAT, "Show me what you mean.", sender, kWatchAction, "", false);
    menu.AddGossipMenuItemData(0, 0, 0);
}

bool HsHordeWarriorHandleSelect(Player* player, Creature* creature, uint32_t action)
{
    if (action != kWatchAction)
        return false;
    if (!player || !player->PlayerTalkClass)
        return true;
    player->PlayerTalkClass->CloseGossip();
    if (!HsHordeWarriorSceneOpen(player, creature) || !player->GetSession() || !player->IsAlive() ||
        !player->IsWithinDistInMap(creature, INTERACTION_DISTANCE))
        return true;
    SceneRequest const request = { player->GetGUIDLow(), player->GetSession()->GetAccountId(),
        creature->GetObjectGuid() };
    std::lock_guard<std::mutex> lock(g_queueMutex);
    g_starts.push_back(request);
    return true;
}

void HsHordeWarriorForget(uint32_t guid)
{
    std::lock_guard<std::mutex> lock(g_queueMutex);
    g_cancels.push_back(guid);
}

void HsHordeWarriorUpdate(uint32_t diff)
{
    std::vector<SceneRequest> starts;
    std::vector<uint32> cancels;
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        starts.swap(g_starts);
        cancels.swap(g_cancels);
    }
    Map* map = sMapMgr.FindMap(1, 0);
    for (uint32 guid : cancels)
        g_scenes.erase(guid);
    if (!map)
    {
        g_scenes.clear();
        return;
    }
    for (SceneRequest const& request : starts)
    {
        if (g_scenes.count(request.guid)) // Selecting again never fast-forwards or resets a running scene.
            continue;
        Player* player = map->GetPlayer(ObjectGuid(HIGHGUID_PLAYER, request.guid));
        Creature* teacher = map->GetCreature(request.teacher);
        if (!HsHordeWarriorSceneOpen(player, teacher) || !player->GetSession() ||
            player->GetSession()->GetAccountId() != request.account || !teacher->IsAlive() ||
            !player->IsAlive() || !teacher->IsWithinDistInMap(player, 25.0f))
            continue;
        Scene scene;
        scene.request = request;
        scene.thinking = FindActor(map, teacher, kHsHordeThinkingGrunt);
        scene.waiting = FindActor(map, teacher, kHsHordeWaitingGrunt);
        if (!scene.thinking || !scene.waiting)
        {
            ChatHandler(player->GetSession()).SendSysMessage("The two Hearthstone players must be nearby "
                "before the demonstration can start.");
            continue;
        }
        g_scenes.emplace(request.guid, scene);
    }
    for (auto it = g_scenes.begin(); it != g_scenes.end();)
    {
        Scene& scene = it->second;
        Player* player = map->GetPlayer(ObjectGuid(HIGHGUID_PLAYER, it->first));
        Creature* teacher = map->GetCreature(scene.request.teacher);
        Creature* thinking = map->GetCreature(scene.thinking);
        Creature* waiting = map->GetCreature(scene.waiting);
        if (!HsHordeWarriorSceneOpen(player, teacher) || !player->GetSession() ||
            player->GetSession()->GetAccountId() != scene.request.account || !player->IsAlive() ||
            !teacher->IsAlive() || !thinking || !thinking->IsAlive() || !waiting || !waiting->IsAlive() ||
            !teacher->IsWithinDistInMap(player, 25.0f))
        {
            if (player && player->GetSession() && player->GetQuestStatus(kHsHordeWarriorWatch) == QUEST_STATUS_INCOMPLETE)
                ChatHandler(player->GetSession()).SendSysMessage("The demonstration stopped. "
                    "Talk to the veteran to start again.");
            it = g_scenes.erase(it);
            continue;
        }
        bool const finished = scene.clock.Advance(diff, [&](uint32 line)
        {
            switch (line)
            {
                case 0: teacher->MonsterSayToPlayer("Watch these two. This is what the game's become.", player); break;
                case 1: waiting->MonsterSayToPlayer("Did you take your turn?", player); break;
                case 2: thinking->MonsterSayToPlayer("No. I'm thinking.", player); break;
                case 3: waiting->MonsterSayToPlayer("You said that twenty minutes ago.", player); break;
                case 4: thinking->MonsterSayToPlayer("I was thinking then.", player); break;
                case 5: waiting->MonsterSayToPlayer("I need to leave soon.", player); break;
                case 6: thinking->MonsterSayToPlayer("Yes, I just don't know what to do.", player); break;
                case 7: teacher->MonsterYell("JUST HIT HIM!", LANG_UNIVERSAL, player); break;
            }
        });
        if (finished)
        {
            player->AreaExploredOrEventHappens(kHsHordeWarriorWatch);
            it = g_scenes.erase(it);
        }
        else
            ++it;
    }

}

void AddHsHordeWarriorQuestScripts()
{
    new HordeDeckPickupScript();
}

void HsHordeWarriorAfterQuestChange(Player* player)
{
    // The core's removal does not always take required items; also clear a banked deck.
    if (!player || player->GetQuestStatus(kHsHordeWarriorDeckQuest) != QUEST_STATUS_NONE ||
        player->GetQuestRewardStatus(kHsHordeWarriorDeckQuest))
        return;
    if (uint32 const count = player->GetItemCount(kHsHordeVeteranDeckItem, true))
        player->DestroyItemCount(kHsHordeVeteranDeckItem, count, true, false, true);
}
