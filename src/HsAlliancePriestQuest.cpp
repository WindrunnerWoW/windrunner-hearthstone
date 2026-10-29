#include "HsAlliancePriestQuest.h"
#include "quests/HsAlliancePriestRules.h"
#include "Camera.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GossipDef.h"
#include "Item.h"
#include "Map.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "TemporarySummon.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace
{
    struct RewardSnapshot
    {
        ObjectGuid teacher;
        uint32 account = 0, map = 0, instance = 0;
    };
    // Packet transport and visual-actor lifetime only; native reward status is authoritative.
    // Do not call core APIs while this mutex is held.
    std::mutex g_sceneMutex;
    std::unordered_map<uint32, RewardSnapshot> g_pendingRewards;
    std::unordered_map<uint32, ObjectGuid> g_scenes;

    bool CurrentScene(uint32 owner, ObjectGuid actor = ObjectGuid())
    {
        std::lock_guard<std::mutex> lock(g_sceneMutex);
        auto const it = g_scenes.find(owner);
        return it != g_scenes.end() && (!actor || it->second == actor);
    }

    void EndScene(uint32 owner, ObjectGuid actor)
    {
        std::lock_guard<std::mutex> lock(g_sceneMutex);
        auto const it = g_scenes.find(owner);
        if (it != g_scenes.end() && it->second == actor) g_scenes.erase(it);
    }

    void IdleSceneActor(Creature* actor)
    {
        actor->StopMoving();
        actor->GetMotionMaster()->Clear(false, true);
        actor->GetMotionMaster()->MoveIdle();
    }

    class ClaspSceneAI final : public CreatureAI
    {
    public:
        explicit ClaspSceneAI(Creature* actor) : CreatureAI(actor) {}

        bool Begin(RewardSnapshot const& reward)
        {
            if (m_started) return false;
            m_started = true;
            m_teacher = reward.teacher;
            m_account = reward.account;
            auto const& point = kHsAlliancePriestSceneMeeting;
            m_creature->SetWalk(true);
            m_creature->GetMotionMaster()->MovePoint(100, point.x, point.y, point.z,
                MOVE_PATHFINDING | MOVE_WALK_MODE);
            auto const end = m_creature->movespline->FinalDestination();
            float const dx = end.x - point.x, dy = end.y - point.y;
            if (!HsAlliancePriestSceneEndpointAllowed(std::sqrt(dx * dx + dy * dy), std::fabs(end.z - point.z)))
                return false;
            // Native pathfinding can adjust Z. Confirm arrival at its accepted endpoint.
            m_destination = {end.x, end.y, end.z, point.facing};
            return true;
        }

        void Cancel()
        {
            if (m_finishing) return;
            m_finishing = true;
            auto* actor = static_cast<TemporarySummon*>(m_creature);
            EndScene(actor->GetSummonerGuid().GetCounter(), actor->GetObjectGuid());
            actor->UnSummon();
        }

        void UpdateAI(uint32 diff) override
        {
            if (m_finishing) return;
            auto* actor = static_cast<TemporarySummon*>(m_creature);
            ObjectGuid const ownerGuid = actor->GetSummonerGuid();
            Player* owner = actor->GetMap()->GetPlayer(ownerGuid);
            Creature* teacher = actor->GetMap()->GetCreature(m_teacher);
            m_elapsed += std::min(diff, kHsAlliancePriestSceneLifetime - m_elapsed);
            bool const identity = m_started && owner && owner->GetSession() && owner->IsInWorld() &&
                owner->GetSession()->GetAccountId() == m_account && owner->GetQuestRewardStatus(kHsAlliancePriestClasp) &&
                teacher && teacher->IsInWorld() && teacher->GetEntry() == kHsAlenya && !teacher->IsTemporarySummon() &&
                teacher->GetMap() == actor->GetMap();
            auto const& meeting = kHsAlliancePriestSceneMeeting;
            bool const nearby = owner && teacher && owner->IsWithinDistInMap(teacher, kHsAlliancePriestSceneRange) &&
                teacher->GetExactDist(meeting.x, meeting.y, meeting.z) <= kHsAlliancePriestSceneTeacherRange;
            if (!HsAlliancePriestSceneContinues(CurrentScene(ownerGuid.GetCounter(), actor->GetObjectGuid()), identity,
                owner && owner->IsAlive() && actor->IsAlive() && teacher && teacher->IsAlive(),
                owner && !owner->IsInCombat() && !actor->IsInCombat() && teacher && !teacher->IsInCombat(), nearby,
                actor->GetMapId(), actor->GetInstanceId(), m_elapsed))
            {
                Cancel();
                return;
            }
            if (m_clock.phase == HsAlliancePriestScenePhase::Walk)
            {
                m_walkElapsed += std::min(diff, kHsAlliancePriestSceneWalkLimit - m_walkElapsed);
                if (m_clock.Arrive(actor->movespline->Finalized(),
                    actor->GetExactDist(m_destination.x, m_destination.y, m_destination.z)))
                {
                    IdleSceneActor(actor);
                    actor->SetFacingTo(meeting.facing);
                }
                else if (m_walkElapsed == kHsAlliancePriestSceneWalkLimit) Cancel();
                return; // Arrival starts a fresh pause; movement time never advances dialogue.
            }
            HsAlliancePriestScenePhase const previous = m_clock.phase;
            if (!m_clock.Advance(diff)) return;
            if (previous == HsAlliancePriestScenePhase::Apology)
                actor->MonsterSayToPlayer("Alenya... I told Maerith I would come back for her. I didn't. I'm sorry.", owner);
            else if (previous == HsAlliancePriestScenePhase::Promise)
                actor->MonsterSayToPlayer("When my watch is over, I'll come back and tell you everything. "
                    "You shouldn't have had to send someone to ask.", owner);
            else if (previous == HsAlliancePriestScenePhase::Reply)
                teacher->MonsterSayToPlayer("That would be wonderful, Syrelle. I'd like to hear it from you.", owner);
            else if (previous == HsAlliancePriestScenePhase::Pause) Cancel();
        }

    private:
        ObjectGuid m_teacher;
        uint32 m_account = 0, m_elapsed = 0, m_walkElapsed = 0;
        HsAlliancePriestScenePoint m_destination{};
        HsAlliancePriestSceneClock m_clock;
        bool m_started = false, m_finishing = false;
    };

    bool IsSceneActor(Creature const* creature)
    {
        return creature && creature->GetEntry() == kHsSyrelle && creature->IsTemporarySummon() &&
            dynamic_cast<ClaspSceneAI const*>(creature->AI());
    }

    void ClaspSceneActor(Creature* actor)
    {
        if (!actor || actor->GetEntry() != kHsSyrelle || !actor->IsTemporarySummon()) return;
        // A custom summon setter replaces AIM_Initialize. Populate MotionMaster before map insertion.
        actor->GetMotionMaster()->Initialize();
        actor->SetAI(new ClaspSceneAI(actor));
        actor->SetReactState(REACT_PASSIVE);
        actor->SetUInt32Value(UNIT_NPC_FLAGS, 0);
        actor->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SPAWNING | UNIT_FLAG_NOT_SELECTABLE);
    }

    class SceneVisibility final : public AllCreatureScript
    {
    public:
        SceneVisibility() : AllCreatureScript("hearthstone_alliance_priest_scene") {}

        void OnCreatureRemoveWorld(Creature* creature) override
        {
            if (!IsSceneActor(creature)) return;
            auto const* actor = static_cast<TemporarySummon const*>(creature);
            EndScene(actor->GetSummonerGuid().GetCounter(), actor->GetObjectGuid());
        }

        bool CanSeeCreature(Player const* player, Creature const* creature) const override
        {
            bool const sceneActor = IsSceneActor(creature);
            bool const ownActor = sceneActor && player &&
                static_cast<TemporarySummon const*>(creature)->GetSummonerGuid() == player->GetObjectGuid() &&
                CurrentScene(player->GetGUIDLow(), creature->GetObjectGuid());
            return HsAlliancePriestSceneVisible(creature->GetEntry(), sceneActor, ownActor);
        }
    };

    bool OurEntry(uint32 entry)
    {
        return entry == kHsAlenya || entry == kHsSyrelle;
    }

    bool Eligible(Player const* player, uint32 entry)
    {
        return player && OurEntry(entry) && player->GetTeam() == ALLIANCE && player->GetLevel() >= 10 &&
            player->GetQuestRewardStatus(919714);
    }

    bool Open(Player const* player, uint32 quest)
    {
        uint32 const previous = quest == kHsAlliancePriestOrder ? 919714 : quest - 1;
        auto const state = player ? player->GetQuestStatus(quest) : QUEST_STATUS_NONE;
        return player && HsAlliancePriestLessonAllowed(player->GetTeam() == ALLIANCE, player->GetLevel(),
            player->GetQuestRewardStatus(919714), state == QUEST_STATUS_INCOMPLETE || state == QUEST_STATUS_COMPLETE,
            player->GetQuestRewardStatus(quest), player->GetQuestRewardStatus(previous));
    }

    uint32 Step(Player const* player)
    {
        auto const* data = player ? player->GetQuestStatusData(kHsAlliancePriestOrder) : nullptr;
        return data ? HsAlliancePriestNextStep({data->m_creatureOrGOcount[0], data->m_creatureOrGOcount[1]}) : 2;
    }

    uint32 Exchange(Player const* player, uint32 entry)
    {
        if (!Eligible(player, entry)) return 3;
        uint32 index = 3;
        if (Open(player, kHsAlliancePriestOrder) &&
            player->GetQuestStatus(kHsAlliancePriestOrder) == QUEST_STATUS_INCOMPLETE)
        {
            uint32 const step = Step(player);
            if (step < kHsAlliancePriestCredits.size()) index = step;
        }
        else if (HsAlliancePriestClaspAllowed(Open(player, kHsAlliancePriestClasp),
            player->GetItemCount(kHsMaerithClasp, true) != 0))
            index = 2;
        return HsAlliancePriestExchangeCreature(index, entry) ? index : 3;
    }

    void Option(GossipMenu& menu, char const* label, uint32 sender, uint32 action)
    {
        if (menu.MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS) return;
        menu.AddMenuItem(GOSSIP_ICON_CHAT, label, sender, action, "", false);
        menu.AddGossipMenuItemData(0, 0, 0);
    }

    void Close(Player* player)
    {
        player->PlayerTalkClass->ClearMenus();
        player->PlayerTalkClass->CloseGossip();
    }

    struct Conversation
    {
        uint32 firstText;
        char const* replies[3];
    };

    Conversation const kConversations[] = {
        {9195402, {"She wants your account of what happened.", "Were you able to get back?",
            "I'll tell her what happened, including the clasp."}},
        {9195407, {"She was cut off before she could relieve Maerith. She still has her cloak clasp.",
            nullptr, nullptr}},
        {9195412, {"She asked me to bring it back.", "Keeping it won't change that night.",
            "I'll bring it to her as it is."}},
    };

    bool GiveClasp(Player* player, bool offeredFinalReply)
    {
        bool const open = Open(player, kHsAlliancePriestClasp);
        bool const hasClasp = player->GetItemCount(kHsMaerithClasp, true) != 0;
        if (!HsAlliancePriestClaspAllowed(open, hasClasp) || !offeredFinalReply) return false;
        ItemPosCountVec destination;
        InventoryResult const result = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, kHsMaerithClasp, 1);
        if (!HsAlliancePriestClaspGrantAllowed(open, hasClasp, offeredFinalReply, result == EQUIP_ERR_OK))
        {
            player->SendEquipError(result, nullptr, nullptr, kHsMaerithClasp);
            return false;
        }
        Item* item = player->StoreNewItem(destination, kHsMaerithClasp, true);
        if (!item) return false;
        player->SendNewItem(item, 1, true, false);
        return true;
    }

    bool RewardPacket(WorldPacket const& packet, ObjectGuid& teacher)
    {
        if (packet.GetOpcode() != CMSG_QUESTGIVER_CHOOSE_REWARD ||
            packet.size() < sizeof(uint64) + 2 * sizeof(uint32) ||
            packet.read<uint32>(sizeof(uint64)) != kHsAlliancePriestClasp) return false;
        WorldPacket copy(packet);
        copy.rpos(0);
        copy >> teacher;
        return teacher.IsAnyTypeCreature();
    }

    bool SceneInteraction(Player* player, Creature* teacher)
    {
        auto const& meeting = kHsAlliancePriestSceneMeeting;
        return player && teacher && player->GetSession() && player->IsInWorld() && teacher->IsInWorld() &&
            player->IsAlive() && teacher->IsAlive() && !player->IsInCombat() && !teacher->IsInCombat() &&
            teacher->GetEntry() == kHsAlenya && !teacher->IsTemporarySummon() &&
            player->GetMap() == teacher->GetMap() && player->IsWithinDistInMap(teacher, INTERACTION_DISTANCE) &&
            player->IsWithinLOSInMap(teacher) &&
            teacher->GetExactDist(meeting.x, meeting.y, meeting.z) <= kHsAlliancePriestSceneTeacherRange;
    }

    void StartScene(Player* player, RewardSnapshot const& reward)
    {
        uint32 const owner = player->GetGUIDLow();
        {
            std::lock_guard<std::mutex> lock(g_sceneMutex);
            if (!g_scenes.emplace(owner, ObjectGuid()).second) return; // Never reset a running scene.
        }
        auto const& spawn = kHsAlliancePriestSceneSpawn;
        Creature* actor = player->SummonCreature(kHsSyrelle, spawn.x, spawn.y, spawn.z, spawn.facing,
            TEMPSUMMON_TIMED_DESPAWN, kHsAlliancePriestSceneLifetime, true, 0, ClaspSceneActor);
        ObjectGuid const actorGuid = actor ? actor->GetObjectGuid() : ObjectGuid();
        bool published = false;
        {
            std::lock_guard<std::mutex> lock(g_sceneMutex);
            auto const it = g_scenes.find(owner);
            if (it != g_scenes.end() && !it->second)
            {
                if (actor) { it->second = actorGuid; published = true; }
                else g_scenes.erase(it);
            }
        }
        if (!actor) return;
        auto* ai = static_cast<ClaspSceneAI*>(actor->AI());
        if (!published || !ai->Begin(reward))
        {
            ai->Cancel();
            return;
        }
        // The actor was hidden until its GUID was published; refresh only this owner's camera.
        player->GetCamera().UpdateVisibilityForOwner();
    }
}

void AddHsAlliancePriestQuestScripts()
{
    new SceneVisibility();
}

bool HsAlliancePriestGossipActive(Player* player, Creature* creature)
{
    return creature && !creature->IsTemporarySummon() && Eligible(player, creature->GetEntry());
}

void HsAlliancePriestAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender)
{
    if (!HsAlliancePriestGossipActive(player, creature)) return;
    uint32 const entry = creature->GetEntry();
    uint32 greeting = entry == kHsAlenya ? 65080 : 65081;
    if (entry == kHsAlenya)
    {
        if (player->GetQuestRewardStatus(kHsAlliancePriestDuel)) greeting = 65095;
        else if (Open(player, kHsAlliancePriestDuel))
            greeting = player->GetQuestStatus(kHsAlliancePriestDuel) == QUEST_STATUS_COMPLETE ? 65094 : 65093;
        else if (Open(player, kHsAlliancePriestClasp)) greeting = 65088;
        else if (player->GetQuestRewardStatus(kHsAlliancePriestClasp)) greeting = 65091;
        else if (Open(player, kHsAlliancePriestOrder))
            greeting = Step(player) == 0 ? 65084 : Step(player) == 1 ? 65085 : 65086;
        else if (player->GetQuestRewardStatus(kHsAlliancePriestOrder)) greeting = 65087;
    }
    else
    {
        if (player->GetQuestRewardStatus(kHsAlliancePriestDuel)) greeting = 65096;
        else if (player->GetQuestRewardStatus(kHsAlliancePriestClasp)) greeting = 65092;
        else if (Open(player, kHsAlliancePriestClasp))
            greeting = player->GetItemCount(kHsMaerithClasp, true) ? 65090 : 65089;
        else if (Open(player, kHsAlliancePriestOrder)) greeting = Step(player) == 0 ? 65082 : 65083;
        else if (player->GetQuestRewardStatus(kHsAlliancePriestOrder)) greeting = 65083;
    }
    menu.SetMenuId(greeting);
    uint32 const index = Exchange(player, entry);
    if (index < 3)
        Option(menu, kConversations[index].replies[0], sender, kHsAlliancePriestActionFirst + index * 4 + 1);
}

bool HsAlliancePriestHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender)
{
    if (action < kHsAlliancePriestActionFirst || action > kHsAlliancePriestActionLast) return false;
    if (!player || !player->PlayerTalkClass) return true;
    uint32 const index = Exchange(player, creature ? creature->GetEntry() : 0);
    auto& menu = player->PlayerTalkClass->GetGossipMenu();
    bool offered = false;
    for (uint32 i = 0; i < menu.MenuItemCount(); ++i)
        offered |= menu.MenuItemSender(i) == sender && menu.MenuItemAction(i) == action;
    if (!creature || !HsAlliancePriestGossipActive(player, creature) || !player->GetSession() ||
        menu.GetMenuSession() != player->GetSession() || !player->IsInWorld() || !creature->IsInWorld() ||
        !player->IsAlive() || !creature->IsAlive() || player->GetMap() != creature->GetMap() ||
        player->GetSession()->GetCurrentGossipGUID() != creature->GetObjectGuid() ||
        !player->IsWithinDistInMap(creature, INTERACTION_DISTANCE) || !player->IsWithinLOSInMap(creature) ||
        !HsAlliancePriestActionAllowed(index, action, offered))
    {
        Close(player);
        return true;
    }

    uint32 const page = action == kHsAlliancePriestRetry ? 3 : (action - kHsAlliancePriestActionFirst) % 4;
    auto const& conversation = kConversations[index];
    bool const last = page + 1 == HsAlliancePriestPageCount(index);
    if (last && index == 2 && !GiveClasp(player, offered))
    {
        // The final acknowledgement remains retryable; no item or other state changes on failure.
        player->PlayerTalkClass->ClearMenus();
        Option(player->PlayerTalkClass->GetGossipMenu(), "There's room now. I'll take it.", sender,
            kHsAlliancePriestRetry);
        player->PlayerTalkClass->SendGossipMenu(9195416, creature->GetObjectGuid());
        return true;
    }

    player->PlayerTalkClass->ClearMenus();
    if (!last)
        Option(player->PlayerTalkClass->GetGossipMenu(), conversation.replies[page], sender, action + 1);
    else
    {
        if (index < kHsAlliancePriestCredits.size()) player->KilledMonsterCredit(kHsAlliancePriestCredits[index]);
        player->PrepareQuestMenu(creature->GetObjectGuid());
    }
    player->PlayerTalkClass->SendGossipMenu(conversation.firstText + page, creature->GetObjectGuid());
    return true;
}

void HsAlliancePriestAfterQuestChange(Player* player)
{
    if (!player) return;
    if (player->GetQuestStatus(kHsAlliancePriestClasp) == QUEST_STATUS_NONE &&
        !player->GetQuestRewardStatus(kHsAlliancePriestClasp))
    {
        uint32 const count = player->GetItemCount(kHsMaerithClasp, true);
        if (count) player->DestroyItemCount(kHsMaerithClasp, count, true, false, true);
    }
    if (!player->PlayerTalkClass) return;
    auto& menu = player->PlayerTalkClass->GetGossipMenu();
    for (uint32 i = 0; i < menu.MenuItemCount(); ++i)
    {
        uint32 const action = menu.MenuItemAction(i);
        if (action < kHsAlliancePriestActionFirst || action > kHsAlliancePriestActionLast) continue;
        uint32 const quest = action < kHsAlliancePriestActionFirst + 8 ?
            kHsAlliancePriestOrder : kHsAlliancePriestClasp;
        if (player->GetQuestStatus(quest) != QUEST_STATUS_NONE) continue;
        // CloseGossip alone leaves the old replies selectable after a later acceptance.
        Close(player);
        return;
    }
}

bool HsAlliancePriestCanInteractPacket(Player* player, WorldPacket const& packet)
{
    switch (packet.GetOpcode())
    {
        case CMSG_GOSSIP_HELLO: case CMSG_GOSSIP_SELECT_OPTION:
        case CMSG_QUESTGIVER_HELLO: case CMSG_QUESTGIVER_QUERY_QUEST:
        case CMSG_QUESTGIVER_ACCEPT_QUEST: case CMSG_QUESTGIVER_COMPLETE_QUEST:
        case CMSG_QUESTGIVER_REQUEST_REWARD: case CMSG_QUESTGIVER_CHOOSE_REWARD: break;
        default: return true;
    }
    if (!player || !player->IsInWorld() || packet.size() < sizeof(uint64)) return true;
    WorldPacket copy(packet);
    copy.rpos(0);
    ObjectGuid guid;
    copy >> guid;
    if (!guid.IsAnyTypeCreature() || !IsSceneActor(player->GetMap()->GetCreature(guid))) return true;
    if (player->PlayerTalkClass) Close(player);
    return false;
}

void HsAlliancePriestBeforeTurnIn(Player* player, WorldPacket const& packet)
{
    if (!player) return;
    uint32 const owner = player->GetGUIDLow();
    ObjectGuid teacherGuid;
    bool const correctPacket = RewardPacket(packet, teacherGuid);
    Creature* teacher = correctPacket && player->IsInWorld() ? player->GetMap()->GetCreature(teacherGuid) : nullptr;
    bool const pending = Eligible(player, kHsAlenya) && SceneInteraction(player, teacher) &&
        player->GetMapId() == kHsAlliancePriestSceneMap && player->GetInstanceId() == kHsAlliancePriestSceneInstance &&
        player->GetQuestStatus(kHsAlliancePriestClasp) == QUEST_STATUS_COMPLETE &&
        !player->GetQuestRewardStatus(kHsAlliancePriestClasp);
    RewardSnapshot reward;
    if (pending) reward = {teacherGuid, player->GetSession()->GetAccountId(), player->GetMapId(), player->GetInstanceId()};
    std::lock_guard<std::mutex> lock(g_sceneMutex);
    g_pendingRewards.erase(owner);
    if (pending) g_pendingRewards.emplace(owner, reward);
}

void HsAlliancePriestAfterTurnIn(Player* player, WorldPacket const& packet)
{
    if (!player) return;
    uint32 const owner = player->GetGUIDLow();
    RewardSnapshot reward;
    bool pending = false;
    {
        std::lock_guard<std::mutex> lock(g_sceneMutex);
        auto const it = g_pendingRewards.find(owner);
        if (it != g_pendingRewards.end())
        {
            reward = it->second;
            g_pendingRewards.erase(it);
            pending = true;
        }
    }
    ObjectGuid teacherGuid;
    if (!pending || !player->IsInWorld() || !RewardPacket(packet, teacherGuid) || teacherGuid != reward.teacher) return;
    Creature* teacher = player->GetMap()->GetCreature(reward.teacher);
    bool const identity = player->GetSession() && player->GetSession()->GetAccountId() == reward.account &&
        player->GetMapId() == reward.map && player->GetInstanceId() == reward.instance && Eligible(player, kHsAlenya);
    if (!HsAlliancePriestSceneStartAllowed(pending, false, player->GetQuestRewardStatus(kHsAlliancePriestClasp),
        identity, SceneInteraction(player, teacher), CurrentScene(owner), player->GetMapId(), player->GetInstanceId())) return;
    StartScene(player, reward);
}

void HsAlliancePriestForget(uint32_t guid)
{
    std::lock_guard<std::mutex> lock(g_sceneMutex);
    g_pendingRewards.erase(guid);
    g_scenes.erase(guid); // The actor cancels next map update; login never replays a rewarded scene.
}
