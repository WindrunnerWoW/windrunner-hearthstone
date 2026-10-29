#include "HsHordeMageQuest.h"
#include "quests/HsHordeMageRules.h"
#include "Creature.h"
#include "DynamicObject.h"
#include "GossipDef.h"
#include "Map.h"
#include "MapManager.h"
#include "Player.h"
#include "Spell.h"
#include "WorldSession.h"

#include <cmath>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr uint32 kExperimentFirst = 0x48530080u;
    constexpr uint32 kReadyFirst = 0x48530083u;
    constexpr uint32 kReadyTextFirst = 9195141;
    constexpr uint32 kMyCardAction = 0x48530086u;
    constexpr uint32 kControlSampleMenu = 65009;
    constexpr uint32 kControlSampleText = 9195145;
    constexpr uint32 kWasText = 9195146;
    char const* const kOptions[] = {
        "Begin the arcane experiment.", "Begin the frost experiment.", "Begin the fire experiment."
    };
    constexpr uint32 kSpells[] = { 1449, 10, 2120 }; // Arcane Explosion, Blizzard, Flamestrike, rank 1.

    uint32 NextExperiment(Player* player)
    {
        QuestStatusData const* status = player->GetQuestStatusData(kHsHordeMageExperiments);
        if (!status)
            return 3;
        return HsHordeMageNextExperiment({ status->m_creatureOrGOcount[0],
            status->m_creatureOrGOcount[1], status->m_creatureOrGOcount[2] });
    }

    struct Request
    {
        uint32 guid;
        uint32 account;
        uint32 map;
        uint32 instance;
        ObjectGuid teacher;
        uint32 step;
    };
    struct Scene
    {
        Request request;
        HsHordeMageClock clock;
        ObjectGuid effect;
    };
    // Gossip/logout hooks only enqueue. The world update owns all scenes and effects.
    std::mutex g_mutex;
    std::vector<Request> g_starts;
    std::vector<uint32> g_cancels;
    std::unordered_map<uint32, Scene> g_scenes;

    // A real timed ground visual, visible only to the observing character. Its server radius is zero,
    // so DynamicObject::Update never applies Blizzard/Flamestrike auras or damage to nearby units.
    class ExperimentVisual final : public DynamicObject
    {
    public:
        explicit ExperimentVisual(ObjectGuid viewer) : m_viewer(viewer) {}
        bool IsVisibleForInState(WorldObject const* detector, WorldObject const* viewpoint,
            bool visible) const override
        {
            return detector && detector->GetObjectGuid() == m_viewer &&
                DynamicObject::IsVisibleForInState(detector, viewpoint, visible);
        }
    private:
        ObjectGuid m_viewer;
    };

    void StopEffect(Scene& scene, Map* map)
    {
        if (map && scene.effect)
            if (DynamicObject* effect = map->GetDynamicObject(scene.effect))
                effect->Delete();
        scene.effect.Clear();
    }

    void StartEffect(Scene& scene, Player* player, Creature* teacher)
    {
        uint32 const spell = kSpells[scene.request.step];
        SpellCastTargets targets;
        if (!scene.request.step)
            targets.setUnitTarget(teacher);
        else
        {
            float const x = teacher->GetPositionX() + 1.5f * std::cos(teacher->GetOrientation());
            float const y = teacher->GetPositionY() + 1.5f * std::sin(teacher->GetOrientation());
            float const z = teacher->GetPositionZ();
            targets.setDestination(x, y, z);
            auto* effect = new ExperimentVisual(player->GetObjectGuid());
            if (effect->Create(teacher->GetMap()->GenerateLocalLowGuid(HIGHGUID_DYNAMICOBJECT),
                teacher, spell, EFFECT_INDEX_0, x, y, z, 2000, 0.0f, DYNAMIC_OBJECT_AREA_SPELL))
            {
                effect->SetFloatValue(DYNAMICOBJECT_RADIUS, 3.0f); // Client visual size only.
                teacher->GetMap()->Add(static_cast<DynamicObject*>(effect));
                scene.effect = effect->GetObjectGuid();
            }
            else
                delete effect;
        }
        // Reuse the core's spell-go wire format for the animation, without executing the damaging spell.
        WorldPacket packet(SMSG_SPELL_GO, 64);
        packet << teacher->GetPackGUID() << teacher->GetPackGUID() << spell << uint16(CAST_FLAG_UNKNOWN9);
        packet << uint8(scene.request.step == 0 ? 1 : 0);
        if (!scene.request.step)
            packet << teacher->GetObjectGuid();
        packet << uint8(0) << targets;
        player->GetSession()->SendPacket(&packet);
    }

    bool Valid(Request const& request, Player* player, Creature* teacher)
    {
        return HsHordeMageExperimentsOpen(player, teacher) && player->GetSession() &&
            player->GetSession()->GetAccountId() == request.account && player->IsAlive() &&
            teacher->IsAlive() && teacher->IsWithinDistInMap(player, 25.0f) &&
            NextExperiment(player) == request.step;
    }
}

bool HsHordeMageExperimentsOpen(Player* player, Creature* creature)
{
    return player && creature && player->GetTeam() == HORDE && creature->GetEntry() == kHsHordeMage &&
        player->GetQuestStatus(kHsHordeMageExperiments) == QUEST_STATUS_INCOMPLETE;
}

bool HsHordeMageGossipActive(Player* player, Creature* creature)
{
    return player && creature && player->GetTeam() == HORDE && creature->GetEntry() == kHsHordeMage &&
        (player->GetQuestStatus(kHsHordeMageExperiments) == QUEST_STATUS_INCOMPLETE ||
        (player->GetQuestStatus(kHsHordeMageExperiments) == QUEST_STATUS_COMPLETE &&
            !player->GetQuestRewardStatus(kHsHordeMageExperiments)));
}

void HsHordeMageAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender)
{
    if (!HsHordeMageGossipActive(player, creature) || menu.MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS)
        return;
    if (player->GetQuestStatus(kHsHordeMageExperiments) == QUEST_STATUS_COMPLETE)
    {
        menu.SetMenuId(kControlSampleMenu);
        menu.AddMenuItem(GOSSIP_ICON_CHAT, "That was my card.", sender, kMyCardAction, "", false);
        menu.AddGossipMenuItemData(0, 0, 0);
        return;
    }
    uint32 const step = NextExperiment(player);
    if (step >= 3)
        return;
    menu.AddMenuItem(GOSSIP_ICON_CHAT, kOptions[step], sender, kExperimentFirst + step, "", false);
    menu.AddGossipMenuItemData(0, 0, 0);
}

bool HsHordeMageHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender)
{
    if (action < kExperimentFirst || action > kMyCardAction)
        return false;
    if (!player || !player->PlayerTalkClass)
        return true;
    if (action == kMyCardAction)
    {
        if (HsHordeMageGossipActive(player, creature) &&
            player->GetQuestStatus(kHsHordeMageExperiments) == QUEST_STATUS_COMPLETE &&
            player->IsAlive() && creature->IsAlive() &&
            player->IsWithinDistInMap(creature, INTERACTION_DISTANCE) && player->IsWithinLOSInMap(creature))
        {
            player->PlayerTalkClass->ClearMenus();
            player->PrepareQuestMenu(creature->GetObjectGuid());
            player->PlayerTalkClass->SendGossipMenu(kWasText, creature->GetObjectGuid());
        }
        else
            player->PlayerTalkClass->CloseGossip();
        return true;
    }
    bool const ready = action >= kReadyFirst;
    uint32 const step = action - (ready ? kReadyFirst : kExperimentFirst);
    if (!HsHordeMageExperimentsOpen(player, creature) || NextExperiment(player) != step ||
        !player->GetSession() || !player->IsAlive() || !creature->IsAlive() ||
        !player->IsWithinDistInMap(creature, INTERACTION_DISTANCE) || !player->IsWithinLOSInMap(creature))
    {
        player->PlayerTalkClass->CloseGossip();
        return true;
    }
    if (!ready)
    {
        player->PlayerTalkClass->ClearMenus();
        GossipMenu& menu = player->PlayerTalkClass->GetGossipMenu();
        menu.AddMenuItem(GOSSIP_ICON_CHAT, "Yes. I am ready.", sender, kReadyFirst + step, "", false);
        menu.AddGossipMenuItemData(0, 0, 0);
        player->PlayerTalkClass->SendGossipMenu(kReadyTextFirst + step, creature->GetObjectGuid());
        return true;
    }
    player->PlayerTalkClass->CloseGossip();
    std::lock_guard<std::mutex> lock(g_mutex);
    g_starts.push_back({ player->GetGUIDLow(), player->GetSession()->GetAccountId(), player->GetMapId(),
        player->GetInstanceId(), creature->GetObjectGuid(), step });
    return true;
}

void HsHordeMageForget(uint32_t guid)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_cancels.push_back(guid);
}

void HsHordeMageUpdate(uint32_t diff)
{
    std::vector<Request> starts;
    std::vector<uint32> cancels;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        starts.swap(g_starts);
        cancels.swap(g_cancels);
    }
    for (uint32 guid : cancels)
    {
        auto it = g_scenes.find(guid);
        if (it != g_scenes.end())
        {
            StopEffect(it->second, sMapMgr.FindMap(it->second.request.map, it->second.request.instance));
            g_scenes.erase(it);
        }
    }
    for (Request const& request : starts)
    {
        if (g_scenes.count(request.guid))
            continue; // Repeated clicks cannot reset or fast-forward the active experiment.
        Map* map = sMapMgr.FindMap(request.map, request.instance);
        if (map && Valid(request, map->GetPlayer(ObjectGuid(HIGHGUID_PLAYER, request.guid)),
            map->GetCreature(request.teacher)))
            g_scenes.emplace(request.guid, Scene{ request, {}, {} });
    }
    for (auto it = g_scenes.begin(); it != g_scenes.end();)
    {
        Scene& scene = it->second;
        Map* map = sMapMgr.FindMap(scene.request.map, scene.request.instance);
        Player* player = map ? map->GetPlayer(ObjectGuid(HIGHGUID_PLAYER, it->first)) : nullptr;
        Creature* teacher = map ? map->GetCreature(scene.request.teacher) : nullptr;
        if (!Valid(scene.request, player, teacher))
        {
            StopEffect(scene, map);
            it = g_scenes.erase(it);
            continue;
        }
        // Time before the visual was started is never counted toward watching it.
        uint32 const elapsed = scene.clock.next == 0 ? 0 : diff;
        bool const finished = scene.clock.Advance(elapsed, [&](uint32)
        {
            StartEffect(scene, player, teacher);
        });
        if (!finished)
        {
            ++it;
            continue;
        }
        StopEffect(scene, map);
        player->KilledMonsterCredit(kHsHordeMageCredits[scene.request.step]);
        if (player->GetSession()->GetCurrentGossipGUID() == teacher->GetObjectGuid())
        {
            player->PlayerTalkClass->ClearMenus();
            player->PrepareQuestMenu(teacher->GetObjectGuid());
            HsHordeMageAppendGossip(player, teacher, player->PlayerTalkClass->GetGossipMenu(), 0x4853);
            player->PlayerTalkClass->SendGossipMenu(scene.request.step == 2 ? kControlSampleText : 9195144,
                teacher->GetObjectGuid());
        }
        it = g_scenes.erase(it);
    }
}
