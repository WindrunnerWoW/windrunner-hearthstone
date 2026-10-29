#include "Common.h"
#include "HsAllianceShamanQuest.h"
#include "quests/HsAllianceShamanRules.h"
#include "quests/HsAllianceShamanRoutes.h"
#include "Chat.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GameObject.h"
#include "GossipDef.h"
#include "Map.h"
#include "MapManager.h"
#include "Log.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "TemporarySummon.h"
#include "WorldSession.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
    bool Review(Player const* player)
    {
        if (!player || !player->IsInWorld()) return false;
        auto const status = player->GetQuestStatus(kHsAllianceShamanReview);
        return HsAllianceShamanReviewAllowed(player->GetTeam() == ALLIANCE, player->GetLevel(),
            player->GetQuestRewardStatus(919714), player->GetQuestRewardStatus(kHsAllianceShamanSupplies),
            status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE,
            player->GetQuestRewardStatus(kHsAllianceShamanReview));
    }
    uint32 Step(Player const* player)
    {
        auto const* status = player->GetQuestStatusData(kHsAllianceShamanReview);
        return status ? HsAllianceShamanNextStep(status->m_creatureOrGOcount[0]) : 4;
    }
    class SceneActorsScript final : public AllCreatureScript
    {
    public:
        SceneActorsScript() : AllCreatureScript("hearthstone_alliance_shaman_scene_actors") {}
        void OnCreatureAddWorld(Creature* creature) override
        {
            if (HsAllianceShamanActorAllowed(creature->GetEntry(), creature->IsTemporarySummon())) return;
            // Route-authoring copies must not appear outside the quest. Keep
            // their DB rows/entry paths; only quest-created temporary actors load.
            creature->SetVisibility(VISIBILITY_OFF);
            creature->AddObjectToRemoveList();
        }
    };

    struct Request
    {
        uint32 guid, account, map, instance;
        ObjectGuid teacher;
        uint64 token = 0;
        uint32 step = 0;
    };
    enum class ViewPhase { Idle, Running, Ready, Acknowledged, Cancelling };
    struct View { Request request; ViewPhase phase = ViewPhase::Running; };
    struct Command { Request request; bool acknowledgement = false; };
    struct Walker : HsAllianceShamanWalkState
    {
        std::vector<HsAllianceShamanPoint> points;
        HsAllianceShamanPoint destination = {};
    };
    enum class Phase { Idle, Approach, Speech, Depart, Returning, Ready, Cleanup };
    struct Scene
    {
        Request request;
        HsAllianceShamanPoint home = {};
        float facing = 0;
        Walker john, visitor;
        ObjectGuid actor, chair;
        Phase phase = Phase::Approach;
        HsAllianceShamanSceneClock speech;
        uint32 stage = 0;
        bool firstTick = true, firstSpeechTick = true, removing = false, spawnPending = true;
        uint32 spawnElapsed = 0, age = 0, cleanupElapsed = 0;
    };
    // Hooks enqueue requests and copy transient views. World update owns actors,
    // movement and RP. Leaf mutex: never call a core API while locked.
    std::mutex g_mutex;
    uint64 g_token = 0;
    std::unordered_map<uint32, View> g_views;
    std::unordered_map<uint32, Scene> g_scenes;
    std::vector<Command> g_commands;
    std::vector<Request> g_cancels;

    bool OwnView(uint32 guid, View& view)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto const it = g_views.find(guid);
        if (it == g_views.end()) return false;
        view = it->second; return true;
    }
    bool TeacherView(uint32 map, uint32 instance, ObjectGuid teacher, View& view)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto const& row : g_views)
            if (row.second.request.map == map && row.second.request.instance == instance &&
                row.second.request.teacher == teacher) { view = row.second; return true; }
        return false;
    }
    void ChangeView(Request const& request, ViewPhase phase)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_views.find(request.guid);
        if (it != g_views.end() && it->second.request.token == request.token)
            it->second = {request, phase};
    }
    void RemoveView(Request const& request)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_views.find(request.guid);
        if (it != g_views.end() && it->second.request.token == request.token) g_views.erase(it);
    }
    bool QueueShamanScene(Request request, bool acknowledgement)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_views.find(request.guid);
        if (it != g_views.end())
        {
            if (it->second.request.teacher != request.teacher || it->second.request.map != request.map ||
                it->second.request.instance != request.instance || it->second.request.step != request.step ||
                it->second.phase != (acknowledgement ? ViewPhase::Ready : ViewPhase::Idle)) return false;
            request = it->second.request;
            it->second.phase = acknowledgement ? ViewPhase::Acknowledged : ViewPhase::Running;
        }
        else
        {
            if (acknowledgement) return false;
            for (auto const& row : g_views)
                if (row.second.request.map == request.map && row.second.request.instance == request.instance &&
                    row.second.request.teacher == request.teacher) return false;
            request.token = ++g_token;
            g_views.emplace(request.guid, View{request, ViewPhase::Running});
        }
        g_commands.push_back({request, acknowledgement}); return true;
    }
    bool Valid(Scene const& scene, Player* player, Creature* teacher)
    {
        View view;
        return OwnView(scene.request.guid, view) && view.request.token == scene.request.token &&
            view.phase != ViewPhase::Cancelling && player && teacher && player->IsInWorld() &&
            player->GetSession() && player->GetSession()->GetAccountId() == scene.request.account &&
            player->GetMapId() == scene.request.map && player->GetInstanceId() == scene.request.instance &&
            teacher->GetEntry() == kHsAllianceShaman && player->IsAlive() && teacher->IsAlive() &&
            !teacher->IsInCombat() && player->IsWithinDistInMap(teacher, 35.0f) &&
            player->IsWithinLOSInMap(teacher) && Review(player) && Step(player) == scene.request.step &&
            player->GetQuestStatus(kHsAllianceShamanReview) == QUEST_STATUS_INCOMPLETE;
    }
    void Idle(Creature* actor)
    {
        actor->StopMoving();
        actor->GetMotionMaster()->Clear(false, true);
        actor->GetMotionMaster()->MoveIdle();
    }
    // Native point movement follows the captured route. Validate the floor-
    // adjusted endpoint at launch, then require actual 3D arrival there. The
    // player-authored Z is not the endpoint of a native smooth path.
    bool Walk(Walker& walker, Creature* actor, uint32 diff)
    {
        if (!actor || walker.paused || walker.next >= walker.points.size()) return true;
        auto const& point = walker.points[walker.next];
        if (walker.moving)
        {
            walker.elapsed += std::min(diff, 120000u - walker.elapsed);
            if (walker.Arrive(point.label, actor->movespline->Finalized(),
                actor->GetExactDist(walker.destination.x, walker.destination.y, walker.destination.z)))
            {
                if (walker.paused) Idle(actor);
            }
            else if (walker.elapsed == 120000)
            {
                sLog.outError("Hearthstone Shaman: entry %u timed out at route point %u, endpoint distance %.3f.",
                    actor->GetEntry(), point.label,
                    actor->GetExactDist(walker.destination.x, walker.destination.y, walker.destination.z));
                return false;
            }
        }
        if (!walker.moving && !walker.paused && walker.next < walker.points.size())
        {
            auto const& next = walker.points[walker.next];
            Idle(actor); actor->SetWalk(true);
            actor->GetMotionMaster()->MovePoint(next.label, next.x, next.y, next.z, MOVE_PATHFINDING | MOVE_WALK_MODE);
            auto const endpoint = actor->movespline->FinalDestination();
            walker.destination = {next.label, endpoint.x, endpoint.y, endpoint.z};
            float const dx = endpoint.x - next.x, dy = endpoint.y - next.y;
            if (!HsAllianceShamanPathEndpointAllowed(std::sqrt(dx*dx + dy*dy), std::fabs(endpoint.z - next.z)))
            {
                sLog.outError("Hearthstone Shaman: entry %u route point %u has an unusable path endpoint (%.3f %.3f %.3f), requested (%.3f %.3f %.3f).",
                    actor->GetEntry(), next.label, endpoint.x, endpoint.y, endpoint.z, next.x, next.y, next.z);
                return false;
            }
            walker.moving = true; walker.elapsed = 0;
        }
        return true;
    }
    bool At(Walker const& walker, uint32 label) { return walker.paused && walker.last == label; }
    void Resume(Walker& walker, uint32 stop) { walker.stop = stop; walker.paused = false; }
    void Passive(Creature* actor)
    {
        actor->SetAI(new CreatureAI(actor));
        actor->SetReactState(REACT_PASSIVE);
    }
    Creature* Visitor(Scene const& scene, Map* map) { return map && scene.actor ? map->GetCreature(scene.actor) : nullptr; }
    GameObject* Chair(Scene const& scene, Map* map) { return map && scene.chair ? map->GetGameObject(scene.chair) : nullptr; }
    bool SpawnChair(Scene& scene, Creature* teacher)
    {
        GameObject* chair = teacher->SummonGameObject(kHsAllianceShamanChair,
            kHsShamanChairPosition.x, kHsShamanChairPosition.y, kHsShamanChairPosition.z,
            kHsShamanChairFacing, 0.0f, 0.0f, 0.0f, 0.0f, 600, true);
        if (!chair) return false;
        scene.chair = chair->GetObjectGuid();
        return true;
    }
    void RemoveChair(Scene& scene, Map* map)
    {
        if (GameObject* chair = Chair(scene, map)) chair->Delete();
        scene.chair.Clear();
    }
    bool RemoveVisitor(Scene& scene, Map* map)
    {
        Creature* actor = Visitor(scene, map);
        if (!actor) { scene.actor.Clear(); scene.removing = false; return true; }
        if (!scene.removing)
        {
            static_cast<TemporarySummon*>(actor)->UnSummon(); scene.removing = true;
        }
        return false; // Keep the teacher reservation until actual removal.
    }
    bool Spawn(Scene& scene, Creature* teacher)
    {
        bool const earth = scene.stage < 2;
        auto const& position = earth ? kHsShamanKiblerEntrance : kHsShamanKripEntrance;
        Creature* actor = teacher->SummonCreature(earth ? kHsAllianceShamanWildhammer : kHsAllianceShamanTauren,
            position.x, position.y, position.z, earth ? kHsShamanKiblerFacing : kHsShamanKripFacing,
            TEMPSUMMON_TIMED_DESPAWN, 600000, true, 0, Passive);
        if (!actor) return false;
        scene.actor = actor->GetObjectGuid(); scene.visitor = {};
        if (earth)
        {
            for (auto const& point : kHsShamanKiblerRoute) if (point.label <= 9) scene.visitor.points.push_back(point);
            scene.visitor.stop = kHsAllianceShamanStages[scene.stage].visitorStop;
        }
        else
        {
            for (auto const& point : kHsShamanKripRoute) if (point.label <= 10) scene.visitor.points.push_back(point);
            scene.visitor.stop = kHsAllianceShamanStages[scene.stage].visitorStop;
        }
        scene.spawnPending = false; return true;
    }
    void Begin(Scene& scene, Creature* teacher, uint32 stage)
    {
        scene.stage = stage;
        scene.phase = Phase::Approach; scene.speech = {}; scene.firstSpeechTick = true;
        scene.firstTick = true; scene.spawnElapsed = 0;
        uint32 const stop = kHsAllianceShamanStages[stage].johnStop;
        if (scene.john.points.empty())
        {
            scene.john.points.assign(kHsShamanJohnRoute.begin(), kHsShamanJohnRoute.end());
            scene.john.points.push_back(scene.home);
        }
        if (!At(scene.john, stop)) Resume(scene.john, stop);
        scene.spawnPending = !scene.actor;
        char const* line = stage == 0
            ? "This way. Earth first, then water. Once the spirits are with us, we'll have that oneshot sorted."
            : stage == 2
            ? "Hmph. Wildhammers, always so critical. Come on; the Water Totem'll make a better impression."
            : nullptr;
        if (line)
            teacher->MonsterSayToPlayer(line,
                teacher->GetMap()->GetPlayer(ObjectGuid(HIGHGUID_PLAYER, scene.request.guid)));
    }
    struct Line { bool visitor; char const* text; uint32 pause; uint32 emote; bool visual; };
    Line const kEarth[] = {
        {false,"There she is: my Earth Totem. Ikae calls it a chair, but I reckon that's just the trade name.",7500,EMOTE_ONESHOT_POINT,false},
        {false,"Earth's solid. This is solid. Mostly. A few missing screws won't put the spirits off.",6500,EMOTE_ONESHOT_TALK,false},
        {false,"EARTH TOTEM! Lend me your strength! ...Aye. Hasn't moved an inch. We're off to a solid start.",7500,EMOTE_ONESHOT_SPELLCAST,false},
    };
    Line const kKibler[] = {
        {true,"Hold on a moment. Are ye shouting at a chair?",5000,EMOTE_ONESHOT_POINT,false},
        {false,"An Earth Totem. Stoneclaw, I reckon. It's got a good grip on the floor.",5000,EMOTE_ONESHOT_TALK,false},
        {true,"That's nae Stoneclaw. A totem helps ye speak with the spirits of the earth. Ye can't get there by naming furniture.",7500,EMOTE_ONESHOT_TALK,false},
        {false,"I built it outdoors. Thought that might help.",5000,EMOTE_ONESHOT_TALK,false},
        {true,"Then ye've an outdoor chair. Ask a Wildhammer about the spirits before ye try fire. I'd hate to see what ye've bought for that.",7500,EMOTE_ONESHOT_TALK,false},
    };
    Line const kWater[] = {
        {false,"Now, the Water Totem. Stout oak, iron bands, and freshly filled with bourbon. Nae assembly required.",7000,EMOTE_ONESHOT_POINT,false},
        {false,"A keg holds spirits. A totem calls spirits. Seems a fair place to start.",6500,EMOTE_ONESHOT_TALK,false},
        {false,"WATER SPIRIT! Rise and lend me your power! ...Must be waiting for me to open the tap.",7500,EMOTE_ONESHOT_SPELLCAST,false},
    };
    Line const kKrip[] = {
        {true,"By the Earth Mother, are you calling on the water spirits through a keg?",6000,EMOTE_ONESHOT_POINT,false},
        {false,"Ambassador! Aye, that's my Water Totem. The keg is... the housing.",6000,EMOTE_ONESHOT_TALK,false},
        {true,"Water flows through rivers, rain, and all living things. Its spirits have little reason to answer a brewer's tap.",8000,EMOTE_ONESHOT_TALK,false},
        {false,"The Wildhammer had much the same objection to my Earth Totem.",6000,EMOTE_ONESHOT_TALK,false},
        {true,"Then his counsel was sound. A totem carries a shaman's bond with the elements. You must earn their trust before asking for their strength.",8500,EMOTE_ONESHOT_SPELLCAST,true},
        {false,"So... there's nae shortcut to the lightning?",5000,EMOTE_ONESHOT_TALK,false},
        {true,"The Earth Mother is patient. The elements may teach you, if you can be quiet long enough to listen.",7500,EMOTE_ONESHOT_TALK,false},
    };
    Line const* Lines(uint32 step, uint32& count)
    {
        switch (step)
        {
            case 0: count = sizeof(kEarth)/sizeof(kEarth[0]); return kEarth;
            case 1: count = sizeof(kKibler)/sizeof(kKibler[0]); return kKibler;
            case 2: count = sizeof(kWater)/sizeof(kWater[0]); return kWater;
            default: count = sizeof(kKrip)/sizeof(kKrip[0]); return kKrip;
        }
    }
    void Say(Line const& line, uint32 stage, Player* player, Creature* teacher, Creature* visitor)
    {
        Creature* speaker = line.visitor ? visitor : teacher;
        if (!speaker) return;
        if (line.visitor) speaker->SetFacingToObject(teacher);
        else if ((stage & 1u) && visitor) speaker->SetFacingToObject(visitor);
        speaker->HandleEmote(line.emote); speaker->MonsterSayToPlayer(line.text, player);
        if (line.visual)
        {
            // Lightning Shield animation only, never cast: no aura, damage,
            // radius, mana cost or cooldown. Client appearance needs testing.
            SpellCastTargets targets; targets.setUnitTarget(speaker);
            WorldPacket packet(SMSG_SPELL_GO, 64);
            packet << speaker->GetPackGUID() << speaker->GetPackGUID() << uint32(324) << uint16(CAST_FLAG_UNKNOWN9);
            packet << uint8(1) << speaker->GetObjectGuid() << uint8(0) << targets;
            player->GetSession()->SendPacket(&packet);
        }
    }
    void Ready(Scene& scene, Player* player, Creature* teacher)
    {
        scene.phase = Phase::Ready; ChangeView(scene.request, ViewPhase::Ready);
        teacher->MonsterSayToPlayer("Here we are. Have a think, then tell me what ye learned. I've a few notes to cross out.", player);
    }
    void Cleanup(Scene& scene, Creature* teacher)
    {
        if (scene.phase == Phase::Cleanup) return;
        scene.phase = Phase::Cleanup; scene.cleanupElapsed = 0; ChangeView(scene.request, ViewPhase::Cancelling);
        RemoveChair(scene, teacher ? teacher->GetMap() : sMapMgr.FindMap(scene.request.map, scene.request.instance));
        if (teacher)
        {
            Idle(teacher); teacher->SetHomePosition(scene.home.x, scene.home.y, scene.home.z, scene.facing);
            if (teacher->IsAlive())
            {
                scene.john = {}; scene.john.points.push_back(scene.home); scene.john.stop = 0;
            }
        }
    }
    void Option(GossipMenu& menu, char const* label, uint32 sender, uint32 action)
    {
        if (menu.MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS) return;
        menu.AddMenuItem(GOSSIP_ICON_CHAT, label, sender, action, "", false); menu.AddGossipMenuItemData(0,0,0);
    }
    char const* const kOpen[] = {"I'm ready. Let's see your totems.","Let's pick up where we left off.","Let's pick up where we left off.","Let's pick up where we left off."};
    char const* const kFinalAck = "Listen to the elements before asking for their strength. And keep the chair for sitting.";
}

void AddHsAllianceShamanQuestScripts() { new SceneActorsScript(); }
bool HsAllianceShamanGossipActive(Player* player, Creature* creature)
{
    if (!player || !creature || creature->GetEntry() != kHsAllianceShaman) return false;
    View view;
    return (player->GetMapId() == 0 && player->GetInstanceId() == 0 && Review(player)) || TeacherView(player->GetMapId(),player->GetInstanceId(),creature->GetObjectGuid(),view);
}
void HsAllianceShamanAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender)
{
    if (!HsAllianceShamanGossipActive(player,creature)) return;
    View view;
    bool const reserved = TeacherView(player->GetMapId(),player->GetInstanceId(),creature->GetObjectGuid(),view);
    if (reserved && (view.request.guid != player->GetGUIDLow() ||
        (view.phase != ViewPhase::Idle && view.phase != ViewPhase::Ready)))
    { player->PlayerTalkClass->ClearMenus(); menu.SetMenuId(65029); return; }
    uint32 const step = Step(player);
    if (step == 4) { menu.SetMenuId(65028); return; }
    if (reserved && view.phase == ViewPhase::Ready)
    {
        player->PlayerTalkClass->ClearMenus(); menu.SetMenuId(65037);
        Option(menu,kFinalAck,sender,kHsAllianceShamanActionFirst+step*4+3); return;
    }
    menu.SetMenuId(65030+step); Option(menu,kOpen[step],sender,kHsAllianceShamanActionFirst+step*4);
}
bool HsAllianceShamanHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender)
{
    if (action < kHsAllianceShamanActionFirst || action > kHsAllianceShamanActionLast) return false;
    if (!player || !player->PlayerTalkClass) return true;
    if (!creature || !Review(player) || creature->GetEntry()!=kHsAllianceShaman || !player->GetSession() ||
        !player->IsAlive() || !creature->IsAlive() || player->GetMapId()!=0 || player->GetInstanceId()!=0 ||
        player->GetSession()->GetCurrentGossipGUID()!=creature->GetObjectGuid() ||
        !player->IsWithinDistInMap(creature,INTERACTION_DISTANCE) || !player->IsWithinLOSInMap(creature))
    { player->PlayerTalkClass->CloseGossip(); return true; }
    auto& menu=player->PlayerTalkClass->GetGossipMenu(); bool offered=false;
    for (uint32 i=0;i<menu.MenuItemCount();++i) offered |= menu.MenuItemSender(i)==sender && menu.MenuItemAction(i)==action;
    uint32 const step=Step(player), page=(action-kHsAllianceShamanActionFirst)%4;
    View view; bool const reserved=TeacherView(player->GetMapId(),player->GetInstanceId(),creature->GetObjectGuid(),view);
    bool const own=reserved && view.request.guid==player->GetGUIDLow();
    bool const running=own && view.phase!=ViewPhase::Idle;
    if ((reserved&&!own) || !HsAllianceShamanActionAllowed(step,action,offered,running,own&&view.phase==ViewPhase::Ready))
    { player->PlayerTalkClass->CloseGossip(); return true; }
    Request request={player->GetGUIDLow(),player->GetSession()->GetAccountId(),player->GetMapId(),player->GetInstanceId(),creature->GetObjectGuid(),0,step};
    if (page==0)
    { QueueShamanScene(request,false); player->PlayerTalkClass->CloseGossip(); return true; }
    if (page==3)
    {
        if (!QueueShamanScene(request,true)) { player->PlayerTalkClass->CloseGossip(); return true; }
        player->KilledMonsterCredit(kHsAllianceShamanCredit);
        player->PlayerTalkClass->ClearMenus(); player->PrepareQuestMenu(creature->GetObjectGuid());
        player->PlayerTalkClass->SendGossipMenu(9195298,creature->GetObjectGuid()); return true;
    }
    player->PlayerTalkClass->CloseGossip(); return true;
}
bool HsAllianceShamanCanInteractPacket(Player* player, WorldPacket const& packet)
{
    uint32 const opcode=packet.GetOpcode();
    bool const gossip=opcode==CMSG_GOSSIP_HELLO || opcode==CMSG_GOSSIP_SELECT_OPTION;
    switch(opcode)
    {
        case CMSG_GOSSIP_HELLO: case CMSG_GOSSIP_SELECT_OPTION: case CMSG_QUESTGIVER_HELLO:
        case CMSG_QUESTGIVER_QUERY_QUEST: case CMSG_QUESTGIVER_ACCEPT_QUEST:
        case CMSG_QUESTGIVER_COMPLETE_QUEST: case CMSG_QUESTGIVER_REQUEST_REWARD: case CMSG_QUESTGIVER_CHOOSE_REWARD: break;
        default:return true;
    }
    if (!player || !player->IsInWorld() || packet.size()<sizeof(uint64)) return true;
    WorldPacket copy(packet); copy.rpos(0); ObjectGuid target; copy>>target;
    View view;
    if (!TeacherView(player->GetMapId(),player->GetInstanceId(),target,view)) return true;
    bool const allowed=HsAllianceShamanReservedInteractionAllowed(view.request.guid==player->GetGUIDLow(),
        view.phase==ViewPhase::Idle, view.phase==ViewPhase::Ready || view.phase==ViewPhase::Acknowledged, gossip);
    if (!allowed&&player->PlayerTalkClass) player->PlayerTalkClass->CloseGossip();
    return allowed;
}
void HsAllianceShamanForget(uint32_t guid)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it=g_views.find(guid); if(it==g_views.end()) return;
    it->second.phase=ViewPhase::Cancelling; g_cancels.push_back(it->second.request);
    g_commands.erase(std::remove_if(g_commands.begin(),g_commands.end(),[guid](Command const& c){return c.request.guid==guid;}),g_commands.end());
}
void HsAllianceShamanAfterQuestChange(Player* player)
{
    if (!player) return;
    if (player->GetQuestStatus(kHsAllianceShamanReview)==QUEST_STATUS_NONE) HsAllianceShamanForget(player->GetGUIDLow());
    if (player->GetQuestStatus(kHsAllianceShamanSupplies)==QUEST_STATUS_NONE)
    {
        uint32 const count=player->GetItemCount(kHsAllianceShamanFittings,true);
        if(count) player->DestroyItemCount(kHsAllianceShamanFittings,count,true,false,true);
    }
}
void HsAllianceShamanUpdate(uint32_t diff)
{
    std::vector<Command> commands; std::vector<Request> cancels;
    {std::lock_guard<std::mutex> lock(g_mutex);commands.swap(g_commands);cancels.swap(g_cancels);}
    for(auto const& cancel:cancels)
    {
        auto it=g_scenes.find(cancel.guid);
        if(it==g_scenes.end()){RemoveView(cancel);continue;}
        if(it->second.request.token!=cancel.token) continue;
        Map* map=sMapMgr.FindMap(cancel.map,cancel.instance);
        Cleanup(it->second,map?map->GetCreature(cancel.teacher):nullptr);
    }
    for(auto const& command:commands)
    {
        auto const& request=command.request;
        View view; if(!OwnView(request.guid,view)||view.request.token!=request.token||view.phase==ViewPhase::Cancelling)continue;
        Map* map=sMapMgr.FindMap(request.map,request.instance);
        Creature* teacher=map?map->GetCreature(request.teacher):nullptr;
        Player* player=map?map->GetPlayer(ObjectGuid(HIGHGUID_PLAYER,request.guid)):nullptr;
        auto it=g_scenes.find(request.guid);
        if(command.acknowledgement)
        {
            if(it==g_scenes.end()||it->second.request.token!=request.token||it->second.phase!=Phase::Ready||
                !player||Step(player)!=4){if(it!=g_scenes.end())Cleanup(it->second,teacher);else RemoveView(request);continue;}
            Cleanup(it->second,teacher);continue;
        }
        if(it==g_scenes.end())
        {
            if(!teacher||!player){RemoveView(request);continue;}
            Scene scene;scene.request=request;
            teacher->GetHomePosition(scene.home.x,scene.home.y,scene.home.z,scene.facing);
            scene.home.label=0;
            it=g_scenes.emplace(request.guid,std::move(scene)).first;
        }
        if(!Valid(it->second,player,teacher)){Cleanup(it->second,teacher);continue;}
        Begin(it->second,teacher,request.step);
    }
    for(auto it=g_scenes.begin();it!=g_scenes.end();)
    {
        Scene& scene=it->second;
        Map* map=sMapMgr.FindMap(scene.request.map,scene.request.instance);
        Creature* teacher=map?map->GetCreature(scene.request.teacher):nullptr;
        Player* player=map?map->GetPlayer(ObjectGuid(HIGHGUID_PLAYER,scene.request.guid)):nullptr;
        if(scene.phase!=Phase::Cleanup)
        {
            scene.age+=std::min(diff,600000u-scene.age);
            if(!Valid(scene,player,teacher)||scene.age==600000) Cleanup(scene,teacher);
        }
        if(scene.phase==Phase::Cleanup)
        {
            scene.cleanupElapsed+=std::min(diff,120000u-scene.cleanupElapsed);
            bool const removed=RemoveVisitor(scene,map);
            bool home=At(scene.john,0)||!teacher||!teacher->IsAlive()||HsAllianceShamanArrived(teacher->movespline->Finalized(),teacher->GetExactDist(scene.home.x,scene.home.y,scene.home.z));
            if(!home)
            {
                bool const walking=Walk(scene.john,teacher,diff);
                if(!walking||scene.cleanupElapsed==120000)
                {Idle(teacher);teacher->NearTeleportTo(scene.home.x,scene.home.y,scene.home.z,scene.facing);home=true;}
            }
            if(removed&&home)
            {
                if(teacher){Idle(teacher);teacher->SetHomePosition(scene.home.x,scene.home.y,scene.home.z,scene.facing);teacher->SetFacingTo(scene.facing);}
                RemoveView(scene.request);it=g_scenes.erase(it);continue;
            }
            ++it;continue;
        }
        uint32 const elapsed=scene.firstTick?0:diff;scene.firstTick=false;
        auto const& stage=kHsAllianceShamanStages[scene.stage];
        if(scene.spawnPending&&scene.phase!=Phase::Idle&&scene.phase!=Phase::Ready &&
            HsAllianceShamanSpawnAllowed(scene.stage,At(scene.john,stage.johnStop)))
        {
            scene.spawnElapsed+=std::min(elapsed,stage.spawnDelay-scene.spawnElapsed);
            if(scene.spawnElapsed==stage.spawnDelay&&!Spawn(scene,teacher)){Cleanup(scene,teacher);++it;continue;}
        }
        Creature* actor=Visitor(scene,map);
        if((scene.actor&&!actor&&!scene.removing)||(actor&&!actor->IsAlive())||
            !Walk(scene.john,teacher,elapsed)||(actor&&!scene.removing&&!Walk(scene.visitor,actor,elapsed)))
        {Cleanup(scene,teacher);++it;continue;}
        if(scene.phase==Phase::Approach)
        {
            if(scene.stage<2 && At(scene.john,stage.johnStop) && !scene.chair && !SpawnChair(scene,teacher))
            {Cleanup(scene,teacher);++it;continue;}
            if(HsAllianceShamanSpeechAllowed(scene.stage, At(scene.john,stage.johnStop),
                bool(actor), At(scene.visitor,stage.visitorStop)))
            {
                scene.phase=Phase::Speech;scene.speech={};scene.firstSpeechTick=true;
                uint32 count;auto const* lines=Lines(scene.stage,count);
                Say(lines[0],scene.stage,player,teacher,actor);
            }
        }
        if(scene.phase==Phase::Speech)
        {
            uint32 count;auto const* lines=Lines(scene.stage,count);
            bool const next=scene.speech.Advance(scene.firstSpeechTick?0:diff,lines[scene.speech.line].pause,count);
            scene.firstSpeechTick=false;
            if(next)
            {
                if(!scene.speech.Ready(count))Say(lines[scene.speech.line],scene.stage,player,teacher,actor);
                else if(scene.stage==0 || scene.stage==2) Begin(scene,teacher,scene.stage+1);
                else {scene.phase=Phase::Depart;Resume(scene.visitor,stage.visitorExit);}
            }
        }
        if(scene.phase==Phase::Depart && (scene.removing||At(scene.visitor,stage.visitorExit)))
        {
            if(RemoveVisitor(scene,map))
            {
                if(scene.stage==1){RemoveChair(scene,map);Begin(scene,teacher,2);}
                else {scene.phase=Phase::Returning;Resume(scene.john,0);teacher->MonsterSayToPlayer("Aye. The chair's a chair, and the keg's a keg. Back to the table. At least I know what the cards are called.",player);}
            }
        }
        if(scene.phase==Phase::Returning&&At(scene.john,0))
        {teacher->SetHomePosition(scene.home.x,scene.home.y,scene.home.z,scene.facing);teacher->SetFacingTo(scene.facing);Ready(scene,player,teacher);}
        ++it;
    }
}
