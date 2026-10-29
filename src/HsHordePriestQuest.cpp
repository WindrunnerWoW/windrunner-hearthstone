#include "HsHordePriestQuest.h"
#include "quests/HsHordePriestRules.h"
#include "GameObject.h"
#include "HsQuestPickup.h"
#include "Map.h"
#include "MapManager.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

#include <cmath>

namespace
{
    bool NeedsItem(Player const* player, uint32 item)
    {
        return player && player->IsInWorld() && player->GetSession() &&
            HsHordePriestPickupAllowed(player->GetTeam() == HORDE,
                player->GetQuestStatus(kHsHordePriestFetch) == QUEST_STATUS_INCOMPLETE,
                player->GetQuestRewardStatus(kHsHordePriestFetch), player->GetLevel(),
                player->GetQuestRewardStatus(919704), player->GetItemCount(item, true));
    }

    // Shared map objects, with visibility derived separately from each viewer's native progress.
    class QuestPickup final : public GameObject
    {
    public:
        explicit QuestPickup(uint32 item) : m_item(item) {}
        bool IsVisibleForInState(WorldObject const* detector, WorldObject const* viewpoint,
            bool visible) const override
        {
            return detector && HsQuestPickupAvailable(this) && NeedsItem(detector->ToPlayer(), m_item) &&
                GameObject::IsVisibleForInState(detector, viewpoint, visible);
        }
    private:
        uint32 m_item;
    };

    std::array<ObjectGuid, kHsHordePriestPositions.size()> g_pickupGuids;
    uint32 g_spawnSweep = 0;

    class PickupInteraction final : public HsQuestPickupAI
    {
    public:
        PickupInteraction(GameObject* go, uint32 item) : HsQuestPickupAI(go, item,
            item == kHsHordePriestItems[0] ? kHsQuestPickupTwoSecondCastIndex :
                kHsQuestPickupHalfSecondCastIndex, true) {}
    protected:
        bool IsEligible(Player const* player) const override { return NeedsItem(player, QuestItem()); }
    };

    class PickupScript final : public AllGameObjectScript
    {
    public:
        PickupScript() : AllGameObjectScript("hearthstone_horde_priest_pickups") {}
        GameObjectAI* GetGameObjectAI(GameObject* go) const override
        {
            for (uint32 i = 0; i < kHsHordePriestObjects.size(); ++i)
                if (go->GetEntry() == kHsHordePriestObjects[i])
                    return new PickupInteraction(go, kHsHordePriestItems[i]);
            return nullptr;
        }
    };
}

void AddHsHordePriestQuestScripts()
{
    new PickupScript();
}

void HsHordePriestUpdate(uint32_t diff)
{
    g_spawnSweep += diff;
    if (g_spawnSweep < 250)
        return;
    g_spawnSweep = 0;
    Map* map = sMapMgr.FindMap(0, 0);
    if (!map)
    {
        g_pickupGuids = {};
        return;
    }
    for (uint32 i = 0; i < kHsHordePriestPositions.size(); ++i)
    {
        HsHordePriestPickupPosition const& position = kHsHordePriestPositions[i];
        uint32 const kind = i / kHsHordePriestRequiredCount;
        uint32 const item = kHsHordePriestItems[kind];
        bool neededNearby = false;
        for (auto const& reference : map->GetPlayers())
        {
            Player* player = reference.getSource();
            if (NeedsItem(player, item) &&
                player->IsWithinDist3d(position.x, position.y, position.z, 100.0f))
            {
                neededNearby = true;
                break;
            }
        }
        GameObject* pickup = map->GetGameObject(g_pickupGuids[i]);
        if (pickup && pickup->IsDeleted())
            continue; // Let the map finish its removal queue before recreating.
        // Keep existing chests through their native respawn even when everyone
        // leaves. Eligibility still hides them; leaving/re-entering cannot reset cooldown.
        if (pickup || !neededNearby)
            continue;
        auto* object = new QuestPickup(item);
        if (!object->Create(map->GenerateLocalLowGuid(HIGHGUID_GAMEOBJECT),
            kHsHordePriestObjects[kind], map, position.x, position.y, position.z,
            position.orientation, 0, 0, std::sin(position.orientation / 2.0f),
            std::cos(position.orientation / 2.0f), 100, GO_STATE_READY))
        {
            delete object;
            continue;
        }
        object->SetRespawnTime(0);
        object->SetRespawnDelay(kHsHordePriestRespawnSeconds);
        g_pickupGuids[i] = object->GetObjectGuid();
        map->Add(static_cast<GameObject*>(object));
    }
}

void HsHordePriestAfterQuestChange(Player* player)
{
    // The core's abandon hook fires before status becomes NONE. The shared packet
    // handled hook calls this afterwards, including when native abandonment fails.
    if (!player || player->GetQuestStatus(kHsHordePriestFetch) != QUEST_STATUS_NONE)
        return;
    for (uint32 item : kHsHordePriestItems)
    {
        uint32 const count = player->GetItemCount(item, true);
        if (count)
            player->DestroyItemCount(item, count, true, false, true);
    }
}
