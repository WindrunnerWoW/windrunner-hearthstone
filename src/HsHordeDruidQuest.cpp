#include "HsHordeDruidQuest.h"
#include "quests/HsHordeDruidRules.h"
#include "quests/HsHordeWarriorRules.h"
#include "Creature.h"
#include "GameObject.h"
#include "HsQuestPickup.h"
#include "GossipDef.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "UpdateData.h"
#include "UpdateMask.h"
#include "WorldSession.h"

#include <cmath>

namespace
{
    constexpr uint32 kAskFeed = 0x48530090u;
    constexpr uint32 kAskBuzzard = 0x48530091u;
    constexpr uint32 kUnderstand = 0x48530092u;
    constexpr uint32 kReportFeed = 0x48530093u;
    constexpr uint32 kReportCards = 0x48530094u;
    constexpr uint32 kFeedAnswered = 0x48530095u;

    uint32 FeedStep(Player* player)
    {
        QuestStatusData const* status = player ? player->GetQuestStatusData(kHsHordeDruidFeedQuest) : nullptr;
        return status ? HsHordeDruidFeedStep({ status->m_creatureOrGOcount[0],
            status->m_creatureOrGOcount[1] }) : 2;
    }

    bool FeedOpen(Player* player)
    {
        return player && player->GetTeam() == HORDE &&
            player->GetQuestStatus(kHsHordeDruidFeedQuest) == QUEST_STATUS_INCOMPLETE && FeedStep(player) == 0;
    }

    bool ReportOpen(Player* player)
    {
        return player && player->GetTeam() == HORDE &&
            player->GetQuestStatus(kHsHordeDruidFeedQuest) == QUEST_STATUS_INCOMPLETE && FeedStep(player) == 1;
    }

    bool AwaitingReward(Player* player, uint32 quest)
    {
        return player->GetQuestStatus(quest) == QUEST_STATUS_COMPLETE && !player->GetQuestRewardStatus(quest);
    }

    void AddOption(GossipMenu& menu, char const* label, uint32 sender, uint32 action)
    {
        menu.AddMenuItem(GOSSIP_ICON_CHAT, label, sender, action, "", false);
        menu.AddGossipMenuItemData(0, 0, 0);
    }

    void ShowExchange(Player* player, Creature* creature, uint32 text, char const* label,
        uint32 sender, uint32 action)
    {
        player->PlayerTalkClass->ClearMenus();
        AddOption(player->PlayerTalkClass->GetGossipMenu(), label, sender, action);
        player->PlayerTalkClass->SendGossipMenu(text, creature->GetObjectGuid());
    }

    bool NeedsCard(Player const* player, uint32 item)
    {
        return player && player->IsInWorld() && HsHordeDruidCardAllowed(player->GetTeam() == HORDE,
            player->GetQuestStatus(kHsHordeDruidCardsQuest) == QUEST_STATUS_INCOMPLETE,
            player->GetItemCount(item, true) != 0);
    }

    // Shared runtime objects use normal map visibility and an additional per-character quest check.
    // There is no owner or ExclusiveVisibleGuid: another eligible character can see and collect the same card.
    class QuestCard final : public GameObject
    {
    public:
        explicit QuestCard(uint32 item) : m_item(item) {}
        bool IsVisibleForInState(WorldObject const* detector, WorldObject const* viewpoint,
            bool visible) const override
        {
            return detector && HsQuestPickupAvailable(this) && NeedsCard(detector->ToPlayer(), m_item) &&
                GameObject::IsVisibleForInState(detector, viewpoint, visible);
        }
    private:
        uint32 m_item;
    };

    // The Warrior box pattern: shared objects, per-viewer flags and authoritative click checks.
    // Each card has its own unique native quest item, so collecting one spot cannot count for the others.
    class CardPickupInteraction final : public HsQuestPickupAI
    {
    public:
        CardPickupInteraction(GameObject* go, uint32 item) : HsQuestPickupAI(go, item) {}
    protected:
        bool IsEligible(Player const* player) const override { return NeedsCard(player, QuestItem()); }
    };

    class HordeDruidPickupScript final : public AllGameObjectScript
    {
    public:
        HordeDruidPickupScript() : AllGameObjectScript("hearthstone_horde_druid_card_pickups") {}
        GameObjectAI* GetGameObjectAI(GameObject* go) const override
        {
            for (uint32 i = 0; i < kHsHordeDruidCardObjects.size(); ++i)
                if (go->GetEntry() == kHsHordeDruidCardObjects[i])
                    return new CardPickupInteraction(go, kHsHordeDruidCardItems[i]);
            return nullptr;
        }
    };

    std::array<ObjectGuid, 4> g_cardGuids;
    uint32 g_spawnSweep = 0; // World-update-owned; no gossip timers or character-local scene state.
}

bool HsHordeDruidGossipActive(Player* player, Creature* creature)
{
    if (!player || !creature || player->GetTeam() != HORDE)
        return false;
    if (creature->GetEntry() == kHsHordeDruidFeedVendor)
        return FeedOpen(player);
    return creature->GetEntry() == kHsHordeDruid &&
        (ReportOpen(player) || AwaitingReward(player, kHsHordeDruidCardsQuest));
}

void HsHordeDruidAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender)
{
    if (!HsHordeDruidGossipActive(player, creature) || menu.MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS)
        return;
    if (creature->GetEntry() == kHsHordeDruidFeedVendor)
        AddOption(menu, "Kolento sent me for the fruit you set aside for his kodo.", sender, kAskFeed);
    else if (ReportOpen(player))
        AddOption(menu, "Nan says a giant buzzard took the kodo's fruit.", sender, kReportFeed);
    else
        AddOption(menu, "I found all four cards. One had blown down to a lower rise.", sender, kReportCards);
}

bool HsHordeDruidHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender)
{
    if (action < kAskFeed || action > kFeedAnswered)
        return false;
    if (!player || !player->PlayerTalkClass)
        return true;
    if (!HsHordeDruidGossipActive(player, creature) || !player->GetSession() || !player->IsAlive() ||
        !creature->IsAlive() || !player->IsWithinDistInMap(creature, INTERACTION_DISTANCE) ||
        !player->IsWithinLOSInMap(creature))
    {
        player->PlayerTalkClass->CloseGossip();
        return true;
    }
    if (creature->GetEntry() == kHsHordeDruidFeedVendor)
    {
        if (action == kAskFeed)
            ShowExchange(player, creature, 9195161, "A giant buzzard?", sender, kAskBuzzard);
        else if (action == kAskBuzzard)
            ShowExchange(player, creature, 9195162, "I'll let Kolento know.", sender, kUnderstand);
        else if (action == kUnderstand)
        {
            player->KilledMonsterCredit(kHsHordeDruidFeedCredit);
            player->PlayerTalkClass->ClearMenus();
            player->PrepareQuestMenu(creature->GetObjectGuid());
            player->PlayerTalkClass->SendGossipMenu(9195163, creature->GetObjectGuid());
        }
        else
            player->PlayerTalkClass->CloseGossip();
        return true;
    }
    bool const feed = action == kReportFeed && ReportOpen(player);
    bool const answered = action == kFeedAnswered && ReportOpen(player);
    bool const cards = action == kReportCards && AwaitingReward(player, kHsHordeDruidCardsQuest);
    if (!feed && !answered && !cards)
    {
        player->PlayerTalkClass->CloseGossip();
        return true;
    }
    if (feed)
    {
        // Show Kolento's answer in its own sub-menu; credit is granted only when the player continues.
        ShowExchange(player, creature, 9195164, "I suppose the kodo will have to wait.", sender, kFeedAnswered);
        return true;
    }
    if (answered)
        player->KilledMonsterCredit(kHsHordeDruidReportCredit);
    player->PlayerTalkClass->ClearMenus();
    player->PrepareQuestMenu(creature->GetObjectGuid());
    player->PlayerTalkClass->SendGossipMenu(answered ? 9195164 : 9195165, creature->GetObjectGuid());
    return true;
}

void HsHordeDruidReconcileFeed(Player* player)
{
    if (!player || player->GetTeam() != HORDE || player->GetQuestRewardStatus(kHsHordeDruidFeedQuest) ||
        player->GetQuestStatus(kHsHordeDruidFeedQuest) != QUEST_STATUS_COMPLETE || FeedStep(player) == 2)
        return;
    Quest const* quest = sObjectMgr.GetQuestTemplate(kHsHordeDruidFeedQuest);
    if (quest && quest->ReqCreatureOrGOId[1] == int32(kHsHordeDruidReportCredit))
        player->IncompleteQuest(kHsHordeDruidFeedQuest);
    // Preserve Nan's saved counter on characters completed under the original one-objective definition.
}

void AddHsHordeDruidQuestScripts()
{
    new HordeDruidPickupScript();
}

void HsHordeDruidUpdate(uint32_t diff)
{
    g_spawnSweep += diff;
    if (g_spawnSweep < 250)
        return;
    g_spawnSweep = 0;
    Map* map = sMapMgr.FindMap(1, 0);
    if (!map)
    {
        g_cardGuids = {};
        return;
    }
    for (uint32 i = 0; i < kHsHordeDruidCardPositions.size(); ++i)
    {
        HsHordeDruidCardPosition const& position = kHsHordeDruidCardPositions[i];
        bool neededNearby = false;
        for (auto const& reference : map->GetPlayers())
        {
            Player* player = reference.getSource();
            if (NeedsCard(player, kHsHordeDruidCardItems[i]) &&
                player->IsWithinDist3d(position.x, position.y, position.z, 100.0f))
            {
                neededNearby = true;
                break;
            }
        }
        GameObject* card = map->GetGameObject(g_cardGuids[i]);
        if (card && card->IsDeleted())
            continue; // Wait for the map's removal queue before recreating it.
        if (!neededNearby)
        {
            if (card)
                card->Delete();
            continue;
        }
        if (card)
            continue;
        auto* pickup = new QuestCard(kHsHordeDruidCardItems[i]);
        if (!pickup->Create(map->GenerateLocalLowGuid(HIGHGUID_GAMEOBJECT), kHsHordeDruidCardObjects[i], map,
            position.x, position.y, position.z, position.orientation, 0, 0,
            std::sin(position.orientation / 2.0f), std::cos(position.orientation / 2.0f), 100, GO_STATE_READY))
        {
            delete pickup;
            continue;
        }
        pickup->SetRespawnTime(0);
        g_cardGuids[i] = pickup->GetObjectGuid();
        map->Add(static_cast<GameObject*>(pickup));
    }
}

void HsHordeDruidAfterQuestChange(Player* player)
{
    // Abandonment clears collected cards from bags and bank; reaccepting starts the search fresh.
    if (!player || player->GetQuestStatus(kHsHordeDruidCardsQuest) != QUEST_STATUS_NONE ||
        player->GetQuestRewardStatus(kHsHordeDruidCardsQuest))
        return;
    for (uint32 const item : kHsHordeDruidCardItems)
        if (uint32 const count = player->GetItemCount(item, true))
            player->DestroyItemCount(item, count, true, false, true);
}
