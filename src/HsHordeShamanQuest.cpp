#include "HsHordeShamanQuest.h"
#include "quests/HsHordeShamanRules.h"
#include "Camera.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GossipDef.h"
#include "Mail.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "TemporarySummon.h"
#include "WaypointManager.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace
{
    // Packet transport snapshot only: never story/objective progress. Native
    // reward status is authoritative; failed packets and logout erase snapshots.
    std::mutex g_rewardMutex;
    std::unordered_set<uint32> g_pendingReward;
    // Transient visual actors only; native reward status remains authoritative.
    std::unordered_map<uint32, ObjectGuid> g_departures;

    bool Departing(uint32 owner, ObjectGuid actor = ObjectGuid())
    {
        std::lock_guard<std::mutex> lock(g_rewardMutex);
        auto const it = g_departures.find(owner);
        return it != g_departures.end() && (!actor || it->second == actor);
    }

    bool EndDeparture(uint32 owner, ObjectGuid actor)
    {
        std::lock_guard<std::mutex> lock(g_rewardMutex);
        auto const it = g_departures.find(owner);
        if (it == g_departures.end() || it->second != actor) return false;
        g_departures.erase(it);
        return true;
    }

    class DepartureAI final : public CreatureAI
    {
    public:
        explicit DepartureAI(Creature* actor) : CreatureAI(actor) {}
        void Begin(uint32 lastPoint) { m_lastPoint = lastPoint; }
        void MovementInform(uint32 type, uint32 point) override
        {
            if (type == WAYPOINT_MOTION_TYPE && point == m_lastPoint) m_arrived = true;
        }
        void UpdateAI(uint32 diff) override
        {
            if (m_finishing) return;
            auto* actor = static_cast<TemporarySummon*>(m_creature);
            ObjectGuid const ownerGuid = actor->GetSummonerGuid();
            Player* owner = actor->GetMap()->GetPlayer(ownerGuid);
            m_elapsed += std::min(diff, kHsAnnieDepartureLimit - m_elapsed);
            if (HsAnnieDepartureContinues(owner && owner->GetQuestRewardStatus(kHsAnnieFarewell),
                owner && owner->IsInWorld() && owner->GetSession(), owner && owner->IsAlive(),
                actor->IsAlive(), owner && actor->IsWithinDistInMap(owner, kHsAnnieDepartureRange),
                Departing(ownerGuid.GetCounter(), actor->GetObjectGuid()), m_arrived, m_elapsed)) return;
            m_finishing = true;
            EndDeparture(ownerGuid.GetCounter(), actor->GetObjectGuid());
            actor->UnSummon();
            if (owner && owner->IsInWorld()) owner->GetCamera().UpdateVisibilityForOwner();
        }
    private:
        uint32 m_lastPoint = 0, m_elapsed = 0;
        bool m_arrived = false, m_finishing = false;
    };

    void DepartureActor(Creature* actor)
    {
        actor->SetAI(new DepartureAI(actor));
        actor->SetReactState(REACT_PASSIVE);
        actor->SetUInt32Value(UNIT_NPC_FLAGS, 0);
        actor->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SPAWNING | UNIT_FLAG_NOT_SELECTABLE);
    }

    void StartDeparture(Player* player, WorldPacket const& packet)
    {
        WorldPacket copy(packet);
        copy.rpos(0);
        ObjectGuid guid;
        copy >> guid;
        Creature* annie = player->GetMap()->GetCreature(guid);
        if (!annie || annie->GetEntry() != kHsAnnie || annie->IsTemporarySummon() ||
            !player->IsAlive() || !annie->IsAlive() ||
            !player->IsWithinDistInMap(annie, INTERACTION_DISTANCE) || !player->IsWithinLOSInMap(annie)) return;
        WaypointPathOrigin origin = PATH_NO_PATH;
        auto const* path = sWaypointMgr.GetDefaultPath(kHsAnnie, annie->GetGUIDLow(), &origin);
        if (!path || path->empty()) return; // Missing route never blocks the rewarded quest.
        uint32 const firstPoint = path->begin()->first, lastPoint = path->rbegin()->first;
        {
            std::lock_guard<std::mutex> lock(g_rewardMutex);
            if (!g_departures.emplace(player->GetGUIDLow(), ObjectGuid()).second) return;
        }
        Creature* actor = player->SummonCreature(kHsAnnie, annie->GetPositionX(), annie->GetPositionY(),
            annie->GetPositionZ(), annie->GetOrientation(), TEMPSUMMON_TIMED_DESPAWN,
            kHsAnnieDepartureLimit, true, 0, DepartureActor);
        {
            std::lock_guard<std::mutex> lock(g_rewardMutex);
            if (actor) g_departures[player->GetGUIDLow()] = actor->GetObjectGuid();
            else g_departures.erase(player->GetGUIDLow());
        }
        if (!actor) return;
        static_cast<DepartureAI*>(actor->AI())->Begin(lastPoint);
        actor->SetWalk(true);
        actor->GetMotionMaster()->MoveWaypoint(firstPoint, origin, 0, annie->GetGUIDLow(), kHsAnnie, false);
    }

    uint32 Lesson(Player* player)
    {
        if (!player) return 0;
        for (uint32 quest : {kHsAnnieGuidance, kHsAnnieFear, kHsAnnieSilence})
        {
            auto const state = player->GetQuestStatus(quest);
            uint32 const previous = quest == kHsAnnieGuidance ? 919704 : quest - 1;
            if (HsAnnieLessonAllowed(player->GetTeam() == HORDE, player->GetLevel(),
                player->GetQuestRewardStatus(919704),
                state == QUEST_STATUS_INCOMPLETE || state == QUEST_STATUS_COMPLETE,
                player->GetQuestRewardStatus(quest), player->GetQuestRewardStatus(previous)))
                return quest;
        }
        return 0;
    }

    std::array<uint32_t, 3> GuidanceCounters(Player* player)
    {
        auto const* data = player->GetQuestStatusData(kHsAnnieGuidance);
        if (!data) return {1, 1, 1};
        return {data->m_creatureOrGOcount[0], data->m_creatureOrGOcount[1], data->m_creatureOrGOcount[2]};
    }

    uint32 Step(Player* player, uint32 quest)
    {
        auto const* data = player->GetQuestStatusData(quest);
        return !data || data->m_creatureOrGOcount[0] ? 1 : 0;
    }

    uint32 Exchange(Player* player, Creature* creature, uint32 quest)
    {
        if (!creature || !quest || player->GetQuestStatus(quest) != QUEST_STATUS_INCOMPLETE) return 5;
        if (quest == kHsAnnieGuidance)
            return HsAnnieMentorExchange(creature->GetEntry(), GuidanceCounters(player));
        uint32 const step = Step(player, quest);
        uint32 const index = step == 0 ? (quest == kHsAnnieFear ? 3 : 4) : 5;
        return HsAnnieExchangeCreature(index, creature->GetEntry()) ? index : 5;
    }

    void Option(GossipMenu& menu, char const* text, uint32 sender, uint32 action)
    {
        if (menu.MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS) return;
        menu.AddMenuItem(GOSSIP_ICON_CHAT, text, sender, action, "", false);
        menu.AddGossipMenuItemData(0, 0, 0);
    }

    struct Conversation
    {
        char const* opening;
        uint32 firstText;
        char const* replies[3];
    };
    Conversation const kConversations[] = {
        {"I'd like to ask about Annie Foxstalker.", 9195310,
            {"She'd like to learn from you.", "I'll help her practice.", nullptr}},
        {"I'd like to ask about Annie Foxstalker.", 9195313,
            {"Where should she begin?", "I'll sit with her.", nullptr}},
        {"I'd like to ask about Annie Foxstalker.", 9195316,
            {"What would you have her try?", "I'll pass that on.", nullptr}},
        {"I'll listen, Annie.", 9195320,
            {"Take your time.", "Tell him that.", "That sounds right."}},
        {"I'm ready to sit with you.", 9195324,
            {"I'll listen too.", "Just the wind so far.", "A little longer."}},
    };

    // This hook hides the shared living creature only from rewarded viewers.
    // Native phase_quest_id independently gates the manually placed spirit.
    class Visibility final : public AllCreatureScript
    {
    public:
        Visibility() : AllCreatureScript("hearthstone_annie_visibility") {}
        void OnCreatureAddWorld(Creature* creature) override
        {
            if (creature->GetEntry() != kHsAnnie) return;
            // Owner-authored waypoints are a farewell route, not idle patrol.
            creature->SetDefaultMovementType(IDLE_MOTION_TYPE);
            creature->GetMotionMaster()->Initialize();
        }
        void OnCreatureRemoveWorld(Creature* creature) override
        {
            if (creature->GetEntry() != kHsAnnie || !creature->IsTemporarySummon()) return;
            ObjectGuid const ownerGuid = static_cast<TemporarySummon*>(creature)->GetSummonerGuid();
            if (!EndDeparture(ownerGuid.GetCounter(), creature->GetObjectGuid())) return;
            Player* owner = creature->GetMap()->GetPlayer(ownerGuid);
            if (owner && owner->IsInWorld()) owner->GetCamera().UpdateVisibilityForOwner();
        }
        bool CanSeeCreature(Player const* player, Creature const* creature) const override
        {
            if (creature->GetEntry() != kHsAnnie && creature->GetEntry() != kHsAnnieSpirit) return true;
            bool const temporary = creature->IsTemporarySummon();
            bool const ownActor = temporary &&
                static_cast<TemporarySummon const*>(creature)->GetSummonerGuid() == player->GetObjectGuid() &&
                Departing(player->GetGUIDLow(), creature->GetObjectGuid());
            return HsAnnieDepartureVisible(creature->GetEntry(), player->GetQuestRewardStatus(kHsAnnieFarewell),
                temporary, ownActor, Departing(player->GetGUIDLow()));
        }
    };

}

void AddHsHordeShamanQuestScripts()
{
    new Visibility();
}

bool HsHordeShamanGossipActive(Player* player, Creature* creature)
{
    if (!player || !creature || !HsAnnieVisible(creature->GetEntry(),
        player->GetQuestRewardStatus(kHsAnnieFarewell))) return false;
    uint32 const quest = Lesson(player);
    if (!quest) return false;
    if (creature->GetEntry() == kHsAnnie) return true;
    if (quest == kHsAnnieGuidance)
        for (uint32 entry : kHsAnnieMentors)
            if (creature->GetEntry() == entry) return true;
    return false;
}

bool HsHordeShamanNeedsEmptyMentorGossip(Player* player, Creature* creature)
{
    if (!creature || Lesson(player) != kHsAnnieGuidance) return false;
    return HsAnnieEmptyMentorGossipAllowed(creature->GetEntry(),
        player->GetQuestStatus(kHsAnnieGuidance) == QUEST_STATUS_INCOMPLETE,
        GuidanceCounters(player));
}

void HsHordeShamanAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender)
{
    if (!HsHordeShamanGossipActive(player, creature)) return;
    uint32 const quest = Lesson(player);
    if (quest == kHsAnnieGuidance)
    {
        if (creature->GetEntry() == kHsAnnie)
        {
            auto const counters = GuidanceCounters(player);
            menu.SetMenuId(counters[0] && counters[1] && counters[2] ? 65040 : 65038);
        }
        else
            menu.SetMenuId(65043 + HsAnnieMentorIndex(creature->GetEntry()));
    }
    else
        menu.SetMenuId(Step(player, quest) == 1 ? (quest == kHsAnnieFear ? 65041 : 65042) : 65038);
    uint32 const index = Exchange(player, creature, quest);
    if (index < 5)
        Option(menu, kConversations[index].opening, sender, kHsAnnieActionFirst + index * 4);
}

bool HsHordeShamanHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender)
{
    if (action < kHsAnnieActionFirst || action > kHsAnnieActionLast) return false;
    if (!player || !player->PlayerTalkClass) return true;
    uint32 const quest = Lesson(player);
    uint32 const index = creature ? Exchange(player, creature, quest) : 5;
    auto& menu = player->PlayerTalkClass->GetGossipMenu();
    bool offered = false;
    for (uint32 i = 0; i < menu.MenuItemCount(); ++i)
        offered |= menu.MenuItemSender(i) == sender && menu.MenuItemAction(i) == action;
    if (!HsHordeShamanGossipActive(player, creature) || !player->GetSession() || !player->IsInWorld() ||
        !player->IsAlive() || !creature->IsAlive() ||
        player->GetSession()->GetCurrentGossipGUID() != creature->GetObjectGuid() ||
        !player->IsWithinDistInMap(creature, INTERACTION_DISTANCE) || !player->IsWithinLOSInMap(creature) ||
        !HsAnnieActionAllowed(index, action, offered))
    {
        player->PlayerTalkClass->CloseGossip();
        return true;
    }
    uint32 const page = (action - kHsAnnieActionFirst) % 4;
    auto const& conversation = kConversations[index];
    player->PlayerTalkClass->ClearMenus();
    if (page + 1 < HsAnniePageCount(index))
        Option(player->PlayerTalkClass->GetGossipMenu(), conversation.replies[page], sender, action + 1);
    else
    {
        // Credit is saved before showing the closing page. No private progress,
        // active scene or conversation survives/requires an open gossip window.
        player->KilledMonsterCredit(kHsAnnieCredits[index]);
        player->PrepareQuestMenu(creature->GetObjectGuid());
    }
    player->PlayerTalkClass->SendGossipMenu(conversation.firstText + page, creature->GetObjectGuid());
    return true;
}

bool HsHordeShamanCanInteractPacket(Player* player, WorldPacket const& packet)
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
    if (!guid.IsAnyTypeCreature()) return true;
    Creature* creature = player->GetMap()->GetCreature(guid);
    if (!creature) return true;
    bool const blockedActor = creature->GetEntry() == kHsAnnie && creature->IsTemporarySummon();
    bool const blockedSpirit = creature->GetEntry() == kHsAnnieSpirit && Departing(player->GetGUIDLow());
    if (!blockedActor && !blockedSpirit &&
        HsAnnieVisible(creature->GetEntry(), player->GetQuestRewardStatus(kHsAnnieFarewell))) return true;
    if (player->PlayerTalkClass) player->PlayerTalkClass->CloseGossip();
    return false;
}

void HsHordeShamanBeforeTurnIn(Player* player, WorldPacket const& packet)
{
    if (!player) return;
    bool const pending = player->IsInWorld() && packet.size() >= sizeof(uint64) + sizeof(uint32) &&
        packet.read<uint32>(sizeof(uint64)) == kHsAnnieFarewell &&
        !player->GetQuestRewardStatus(kHsAnnieFarewell);
    std::lock_guard<std::mutex> lock(g_rewardMutex);
    g_pendingReward.erase(player->GetGUIDLow());
    if (pending) g_pendingReward.insert(player->GetGUIDLow());
}

void HsHordeShamanAfterTurnIn(Player* player, WorldPacket const& packet)
{
    if (!player) return;
    bool pending;
    {
        std::lock_guard<std::mutex> lock(g_rewardMutex);
        pending = g_pendingReward.erase(player->GetGUIDLow()) != 0;
    }
    if (!pending || !player->IsInWorld() || packet.size() < sizeof(uint64) + sizeof(uint32) ||
        packet.read<uint32>(sizeof(uint64)) != kHsAnnieFarewell ||
        !HsAnnieRewardTransition(false, player->GetQuestRewardStatus(kHsAnnieFarewell))) return;
    // Same pre/post native-reward pattern as the introduction. No CreatureScript:
    // marker queries and native gossip/quests retain normal core behavior.
    MailDraft("She Asked for You",
        "When Annie went to help a trader, the quilboar ambushed them. She stood between him and their blades. "
        "She brought down twenty of them. Neither Annie nor the trader survived.\n\n"
        "I had been expecting her to come back and tell me about her day. Instead, "
        "her spirit appeared on Spirit Rise.\n\n"
        "She has been learning Hearthstone with a Shaman deck. She asked me to send for you; "
        "she would like to show you how to play. You will find her here when you have time.\n\nSiln Skychaser")
        .SendMailTo(MailReceiver(player), MailSender(MAIL_CREATURE, kHsAnnieMailSender),
            MAIL_CHECK_MASK_HAS_BODY, kHsAnnieMailDelay);
    StartDeparture(player, packet);
    player->GetCamera().UpdateVisibilityForOwner();
}

void HsHordeShamanForget(uint32_t guid)
{
    std::lock_guard<std::mutex> lock(g_rewardMutex);
    g_pendingReward.erase(guid);
    g_departures.erase(guid); // The actor cancels on its next map update; never replay on login.
}
