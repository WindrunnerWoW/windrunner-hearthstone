#include "HsHunterQuest.h"
#include "quests/HsHunterRules.h"
#include "quests/HsHunterPresentation.h"
#include "Camera.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GossipDef.h"
#include "Map.h"
#include "MapManager.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
#include "Log.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "TemporarySummon.h"
#include "WorldSession.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
    bool Eligible(Player const* player, HsHunterChain const& chain)
    {
        return player && (player->GetTeam() == ALLIANCE) == chain.alliance && player->GetLevel() >= 10 &&
            player->GetQuestRewardStatus(chain.intro);
    }
    bool Open(Player const* player, HsHunterChain const& chain, uint32 quest)
    {
        auto const status = player ? player->GetQuestStatus(quest) : QUEST_STATUS_NONE;
        return player && HsHunterLessonAllowed(player->GetTeam() == ALLIANCE, chain, player->GetLevel(),
            player->GetQuestRewardStatus(chain.intro), status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE,
            player->GetQuestRewardStatus(quest), player->GetQuestRewardStatus(quest == chain.firstQuest ? chain.intro : quest-1));
    }
    uint32 Step(Player const* player, uint32 quest, uint32 count)
    {
        auto const* data = player ? player->GetQuestStatusData(quest) : nullptr;
        return data ? HsHunterNextStep({data->m_creatureOrGOcount[0],data->m_creatureOrGOcount[1],
            data->m_creatureOrGOcount[2]}, count) : count;
    }
    bool Interaction(Player const* player, Creature const* teacher, float distance = INTERACTION_DISTANCE)
    {
        return player && teacher && player->GetSession() && player->IsInWorld() && teacher->IsInWorld() &&
            player->IsAlive() && teacher->IsAlive() && !player->IsInCombat() && !teacher->IsInCombat() &&
            player->GetMapId() == teacher->GetMapId() && player->GetInstanceId() == teacher->GetInstanceId() &&
            player->GetSession()->GetCurrentGossipGUID() == teacher->GetObjectGuid() &&
            player->IsWithinDistInMap(teacher, distance) && player->IsWithinLOSInMap(teacher);
    }
    // Once the trio is shown the player may wander off, close the window or talk to someone else.
    // The scene survives until its own timers run out; re-greeting the teacher restores the choices.
    bool Nearby(Player const* player, Creature const* teacher, float distance)
    {
        return player && teacher && player->GetSession() && player->IsInWorld() && teacher->IsInWorld() &&
            player->IsAlive() && teacher->IsAlive() && !player->IsInCombat() && !teacher->IsInCombat() &&
            player->GetMapId() == teacher->GetMapId() && player->GetInstanceId() == teacher->GetInstanceId() &&
            player->IsWithinDistInMap(teacher, distance);
    }
    void Option(GossipMenu& menu, char const* text, uint32 sender, uint32 action)
    {
        menu.AddMenuItem(GOSSIP_ICON_CHAT,text,sender,action,"",false);
        menu.AddGossipMenuItemData(0,0,0);
    }
    char const* const kChoices[2][3][3] = {
        {{"I'd take the bear.","I'd take the buzzard.","I'd take the tiger."},
         {"I'd take the wolf.","I'd take the boar.","I'd take the turtle."},
         {"I'll try the frog.","I'll take the chicken.","I'll take the accountant."}},
        {{"I'd choose the cat.","I'd choose the owl.","I'd choose the spider."},
         {"I'd choose the crab.","I'd choose the crocolisk.","I'd choose the rabbit."},
         {"I'll take the rat.","I'll take the sheep.","I'll take the accountant."}},
    };
    char const* const kAcknowledgements[2][3] = {
        {"What other companions would you consider?","Tell me your last three.","What did Nali actually ask for?"},
        {"What else have you got?","Show me your final selection.","Did Eris actually ask for a companion?"},
    };
    char const* const kTalk[2][2][3] = {
        {{"What did you ask him to teach you?","And did you choose one?","I'll tell him you want a game."},
         {"She asked to watch you play.","Let her see how you actually play.",nullptr}},
        {{"Fenn sent me. What do you need?","So you don't need a companion?","I'll tell him to stop."},
         {"She wants to read without interruptions.","No lessons at her desk, Fenn.",nullptr}},
    };
    void Page(Player* player, Creature* creature, HsHunterChain const& chain, uint32 offset)
    {
        player->PlayerTalkClass->SendGossipMenu(chain.firstText + offset,creature->GetObjectGuid());
    }
    struct Request
    {
        uint32 guid, account, map, instance, chain, round, sender;
        ObjectGuid teacher;
        uint64 token = 0;
    };
    struct View
    {
        Request request;
        HsHunterPhase phase = HsHunterPhase::Queued;
        std::array<ObjectGuid,3> actors = {};
        uint32 answer = 0;
    };
    enum class CommandKind { Start, Answer, Cancel };
    struct Command { Request request; CommandKind kind; uint32 answer = 0; };
    struct Scene
    {
        Request request;
        std::array<ObjectGuid,3> actors = {};
        uint32 elapsed = 0, age = 0, walkElapsed = 0;
        bool pending = true, captured = false, moving = false, cancelling = false, keepExpired = false;
        uint32 retry = 0;
        HsHunterPoint home = {}, destination = {}, projection = {};
        std::array<HsHunterPoint,3> spots = {};
        HsHunterShow show;
    };
    // Only transport/visual lifetime is transient. Native counters are the sole saved story state.
    // Hooks queue work; world update owns scene actors. Never call the core under the leaf mutex.
    std::mutex g_mutex;
    uint64 g_token = 0;
    std::unordered_map<uint32,View> g_views;
    std::unordered_map<uint32,Scene> g_scenes;
    std::vector<Command> g_commands;
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
            if (row.second.phase != HsHunterPhase::Expired &&
                row.second.request.map == map && row.second.request.instance == instance &&
                row.second.request.teacher == teacher) { view = row.second; return true; }
        return false;
    }
    bool ChangePhase(Request const& request, HsHunterPhase from, HsHunterPhase to)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_views.find(request.guid);
        if (it == g_views.end() || it->second.request.token != request.token || it->second.phase != from) return false;
        it->second.phase = to; return true;
    }
    void RemoveView(Request const& request)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_views.find(request.guid);
        if (it != g_views.end() && it->second.request.token == request.token) g_views.erase(it);
    }
    void CancelView(uint32 guid, uint32 retry = 0)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_views.find(guid);
        if (it == g_views.end()) return;
        g_commands.push_back({it->second.request,CommandKind::Cancel,retry});
        if (it->second.phase != HsHunterPhase::Expired)
            it->second.phase = HsHunterPhase::Cancelling; // Reserve the moving teacher until his return.
        else g_views.erase(it);
    }
    void RemoveActors(Scene& scene)
    {
        Map* map = sMapMgr.FindMap(scene.request.map,scene.request.instance);
        for (ObjectGuid& guid : scene.actors)
        {
            Creature* actor = map && guid ? map->GetCreature(guid) : nullptr;
            if (actor && actor->IsTemporarySummon()) static_cast<TemporarySummon*>(actor)->UnSummon();
            guid.Clear();
        }
    }
    bool ActorsPresent(std::array<ObjectGuid,3> const& actors, Map* map, uint32 owner)
    {
        for (ObjectGuid guid : actors)
        {
            Creature* actor = map && guid ? map->GetCreature(guid) : nullptr;
            if (!actor || !actor->IsInWorld() || !actor->IsAlive() || !actor->IsTemporarySummon() ||
                static_cast<TemporarySummon*>(actor)->GetSummonerGuid() != ObjectGuid(HIGHGUID_PLAYER,owner)) return false;
        }
        return true;
    }
    bool Start(Player* player, Creature* teacher, HsHunterChain const& chain, uint32 round, uint32 sender)
    {
        Request request{player->GetGUIDLow(),player->GetSession()->GetAccountId(),player->GetMapId(),
            player->GetInstanceId(),chain.alliance ? 1u : 0u,round,sender,teacher->GetObjectGuid(),0};
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_views.count(request.guid)) return false; // Duplicate requests never reset a running trio.
        for (auto const& row : g_views)
            if (row.second.phase != HsHunterPhase::Expired && row.second.request.teacher == request.teacher &&
                row.second.request.map == request.map && row.second.request.instance == request.instance) return false;
        request.token = ++g_token;
        g_views.emplace(request.guid,View{request});
        g_commands.push_back({request,CommandKind::Start});
        return true;
    }
    bool Continue(Request const& old, uint32 round)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_views.find(old.guid);
        if (it == g_views.end() || it->second.request.token != old.token ||
            it->second.phase != HsHunterPhase::Reaction) return false;
        Request request = old; request.round = round; request.token = ++g_token;
        it->second = View{request};
        g_commands.push_back({request,CommandKind::Start}); return true;
    }
    void Retry(Player* player, Creature* teacher, HsHunterChain const& chain, uint32 round, uint32 sender, bool expired)
    {
        player->PlayerTalkClass->ClearMenus();
        Option(player->PlayerTalkClass->GetGossipMenu(), expired ?
            (chain.alliance ? "Show me that set again." : "Tell me about those three again.") : "Let's try it again.",
            sender,chain.firstAction + round);
        Page(player,teacher,chain,expired ? 12 : 13);
    }
    bool Valid(Scene const& scene, Player* player, Creature* teacher)
    {
        View view;
        auto const& chain = kHsHunterChains[scene.request.chain];
        bool const own = OwnView(scene.request.guid,view);
        bool const relaxed = view.phase == HsHunterPhase::Choosing || view.phase == HsHunterPhase::Reaction;
        return HsHunterSceneContinues(own && view.request.token == scene.request.token,
            player && player->GetSession() && player->GetSession()->GetAccountId() == scene.request.account &&
                player->GetMapId() == scene.request.map && player->GetInstanceId() == scene.request.instance &&
                teacher && teacher->GetEntry() == chain.teacher,
            view.phase != HsHunterPhase::Cancelling && (relaxed ? Nearby(player,teacher,kHsHunterAwayRange) :
                Interaction(player,teacher,view.phase == HsHunterPhase::Presenting ? 20.0f : INTERACTION_DISTANCE)),
            Open(player,chain,chain.firstQuest) &&
                player->GetQuestStatus(chain.firstQuest) == QUEST_STATUS_INCOMPLETE,
            Step(player,chain.firstQuest,3),scene.request.round);
    }
    void CosmeticActor(Creature* actor)
    {
        // A custom summon setter replaces AIM_Initialize, including its movement setup.
        // Initialize before map insertion so the first Unit::Update has an idle generator.
        actor->GetMotionMaster()->Initialize();
        actor->SetAI(new CreatureAI(actor));
        actor->SetReactState(REACT_PASSIVE);
        actor->SetUInt32Value(UNIT_NPC_FLAGS,0);
        actor->SetFlag(UNIT_FIELD_FLAGS,UNIT_FLAG_SPAWNING | UNIT_FLAG_NOT_SELECTABLE);
    }
    void OfferChoices(Player* player, Creature* teacher, Request const& request)
    {
        auto const& chain = kHsHunterChains[request.chain];
        player->PlayerTalkClass->ClearMenus();
        for (uint32 i = 0; i < 3; ++i)
            Option(player->PlayerTalkClass->GetGossipMenu(),kChoices[request.chain][request.round][i],
                request.sender,chain.firstAction+8+request.round*3+i);
        Page(player,teacher,chain,21+request.round*4);
    }
    HsHunterIntroduction const& Introduction(Scene const& scene)
    {
        return scene.request.chain == 1 ? kHsFennIntroductions[scene.request.round][scene.show.animal] :
            kHsBoroIntroductions[scene.request.round][scene.show.animal];
    }
    void Idle(Creature* teacher)
    {
        teacher->StopMoving(); teacher->GetMotionMaster()->Clear(false,true); teacher->GetMotionMaster()->MoveIdle();
    }
    enum class WalkResult { Moving, Arrived, Failed };
    WalkResult Walk(Scene& scene, Creature* teacher, HsHunterPoint const& point, uint32 diff)
    {
        if (!scene.moving)
        {
            if (teacher->GetExactDist(point.x,point.y,point.z) <= 0.2f) { Idle(teacher); return WalkResult::Arrived; }
            Idle(teacher);
            teacher->GetMotionMaster()->MovePoint(100+scene.show.animal,point.x,point.y,point.z,MOVE_PATHFINDING | MOVE_WALK_MODE);
            auto const end = teacher->movespline->FinalDestination();
            float const dx = end.x-point.x, dy = end.y-point.y;
            if (!HsHunterPathEndpointAllowed(std::sqrt(dx*dx+dy*dy),std::fabs(end.z-point.z))) return WalkResult::Failed;
            scene.destination = {end.x,end.y,end.z,point.facing}; scene.walkElapsed = 0; scene.moving = true;
            return WalkResult::Moving;
        }
        scene.walkElapsed += std::min(diff,kHsHunterWalkLimit-scene.walkElapsed);
        if (HsHunterAtPoint(teacher->movespline->Finalized(),
            teacher->GetExactDist(scene.destination.x,scene.destination.y,scene.destination.z)))
        { Idle(teacher); scene.moving = false; return WalkResult::Arrived; }
        return scene.walkElapsed == kHsHunterWalkLimit ? WalkResult::Failed : WalkResult::Moving;
    }
    bool PublishActor(Scene const& scene, Player* player, uint32 animal)
    {
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_views.find(scene.request.guid);
            if (it == g_views.end() || it->second.request.token != scene.request.token ||
                it->second.phase != HsHunterPhase::Presenting) return false;
            it->second.actors[animal] = scene.actors[animal];
        }
        player->GetCamera().UpdateVisibilityForOwner(); return true;
    }
    bool Reveal(Scene& scene, Player* player, Creature* teacher)
    {
        uint32 const animal = scene.show.animal;
        auto const& point = scene.projection;
        if (!teacher->IsWithinLOS(point.x,point.y,point.z)) return false;
        Creature* actor = player->SummonCreature(kHsHunterChains[scene.request.chain].firstProjection+scene.request.round*3+animal,
            point.x,point.y,point.z,point.facing,TEMPSUMMON_TIMED_DESPAWN,kHsHunterProjectionLifetime+kHsHunterPresentationLimit,
            true,0,CosmeticActor);
        if (!actor) return false;
        scene.actors[animal] = actor->GetObjectGuid();
        if (!PublishActor(scene,player,animal)) return false;
        teacher->HandleEmote(EMOTE_ONESHOT_TALK);
        teacher->MonsterSayToPlayer(Introduction(scene).description,player);
        return true;
    }
    bool Present(Scene& scene, Player* player, Creature* teacher, uint32 diff)
    {
        if (!scene.captured)
        {
            teacher->GetHomePosition(scene.home.x,scene.home.y,scene.home.z,scene.home.facing);
            scene.captured = true;
            scene.spots = scene.request.chain == 1 ? kHsFennSpots :
                std::array<HsHunterPoint,3>{{scene.home,kHsBoroOuterSpots[0],kHsBoroOuterSpots[1]}};
            if (teacher->GetMapId() != 1 || teacher->GetInstanceId() != 0) return false;
            for (auto const& point : scene.spots)
                if (teacher->GetExactDist(point.x,point.y,point.z) > 12.0f) return false;
            if (!ChangePhase(scene.request,HsHunterPhase::Queued,HsHunterPhase::Presenting)) return false;
        }
        scene.age += std::min(diff,kHsHunterPresentationLimit-scene.age);
        if (scene.age == kHsHunterPresentationLimit) return false;
        for (ObjectGuid guid : scene.actors)
            if (guid)
            {
                Creature* actor = teacher->GetMap()->GetCreature(guid);
                if (!actor || !actor->IsInWorld() || !actor->IsAlive()) return false;
            }
        if (scene.show.phase == HsHunterShowPhase::Walk)
        {
            auto const& spot = scene.spots[HsHunterSpot(scene.request.chain == 1,scene.request.round,scene.show.animal)];
            auto const walk = Walk(scene,teacher,spot,diff);
            if (walk == WalkResult::Failed) return false;
            if (walk == WalkResult::Arrived && scene.show.Arrive(true,0))
            {
                // Resolve the floor/collision-adjusted reveal point before pointing at it.
                teacher->GetNearPoint(teacher,scene.projection.x,scene.projection.y,scene.projection.z,0.5f,2.5f,spot.facing);
                auto const& point = scene.projection;
                if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z) ||
                    std::fabs(point.z-teacher->GetPositionZ()) > 2.0f || !teacher->IsWithinLOS(point.x,point.y,point.z)) return false;
                scene.projection.facing = spot.facing;
                teacher->SetFacingTo(teacher->GetAngle(point.x,point.y)); teacher->HandleEmote(EMOTE_ONESHOT_POINT);
                teacher->MonsterSayToPlayer(Introduction(scene).name,player);
            }
            return true;
        }
        auto const phase = scene.show.phase;
        if (!scene.show.Advance(diff)) return true;
        if (phase == HsHunterShowPhase::Name) return Reveal(scene,player,teacher);
        if (scene.show.phase != HsHunterShowPhase::Done) return true;
        if (!ActorsPresent(scene.actors,teacher->GetMap(),scene.request.guid) ||
            !ChangePhase(scene.request,HsHunterPhase::Presenting,HsHunterPhase::Choosing)) return false;
        scene.elapsed = 0;
        teacher->SetFacingToObject(player);
        teacher->MonsterSayToPlayer(scene.request.chain == 1 ?
            "Those are the three I have in mind. Which would you bring her?" :
            "Those are the three I would consider. Which would you choose?",player);
        OfferChoices(player,teacher,scene.request); return true;
    }
    void BeginCleanup(Scene& scene, bool expired = false, uint32 retry = 0)
    {
        RemoveActors(scene);
        if (!scene.cancelling) { scene.cancelling = true; scene.moving = false; }
        scene.keepExpired = expired; scene.retry = retry;
        View view;
        if (OwnView(scene.request.guid,view)) ChangePhase(scene.request,view.phase,HsHunterPhase::Cancelling);
    }
    bool Cleanup(Scene& scene, Creature* teacher, Player* player, uint32 diff)
    {
        if (teacher && teacher->IsInWorld() && teacher->IsAlive() && !teacher->IsInCombat() && scene.captured)
        {
            auto const walk = Walk(scene,teacher,scene.home,diff);
            if (walk == WalkResult::Moving) return false;
            if (walk == WalkResult::Arrived) teacher->SetFacingTo(scene.home.facing);
            else
            {
                sLog.outError("Hearthstone Hunter: teacher %u could not return along his demonstration route; using native home movement.",
                    teacher->GetEntry());
                Idle(teacher); teacher->GetMotionMaster()->MoveTargetedHome();
            }
        }
        auto const& chain = kHsHunterChains[scene.request.chain];
        if (scene.keepExpired && player && teacher && Interaction(player,teacher) && Open(player,chain,chain.firstQuest))
            ChangePhase(scene.request,HsHunterPhase::Cancelling,HsHunterPhase::Expired);
        else RemoveView(scene.request);
        if (scene.retry && player && teacher && Interaction(player,teacher) &&
            Open(player,chain,chain.firstQuest) && player->GetQuestStatus(chain.firstQuest) == QUEST_STATUS_INCOMPLETE)
            Retry(player,teacher,chain,Step(player,chain.firstQuest,3),scene.request.sender,scene.retry == 1);
        return true;
    }
    class Projections final : public AllCreatureScript
    {
    public:
        Projections() : AllCreatureScript("hearthstone_hunter_projections") {}
        void OnCreatureAddWorld(Creature* actor) override
        {
            if (HsHunterProjection(actor->GetEntry()) && !actor->IsTemporarySummon())
            {
                actor->SetVisibility(VISIBILITY_OFF); actor->AddObjectToRemoveList();
            }
        }
        bool CanSeeCreature(Player const* player, Creature const* actor) const override
        {
            if (actor->GetEntry() == kHsHunterChains[0].helper)
                return Open(player,kHsHunterChains[0],kHsHunterChains[0].firstQuest+1);
            if (!HsHunterProjection(actor->GetEntry())) return true;
            if (!actor->IsTemporarySummon()) return false;
            ObjectGuid const owner = static_cast<TemporarySummon const*>(actor)->GetSummonerGuid();
            View view;
            return owner == player->GetObjectGuid() && OwnView(player->GetGUIDLow(),view) &&
                (view.phase == HsHunterPhase::Presenting || view.phase == HsHunterPhase::Choosing) &&
                std::find(view.actors.begin(),view.actors.end(),actor->GetObjectGuid()) != view.actors.end();
        }
    };
}

void AddHsHunterQuestScripts() { new Projections(); }
bool HsHunterGossipActive(Player* player, Creature* creature)
{
    auto const* chain = creature ? HsHunterChainFor(creature->GetEntry()) : nullptr;
    return chain && Eligible(player,*chain); // Custom actor greetings also recover rewarded/unrewarded states.
}
void HsHunterForget(uint32_t guid)
{
    CancelView(guid);
}
void HsHunterAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender)
{
    if (!HsHunterGossipActive(player,creature)) return;
    auto const& chain = *HsHunterChainFor(creature->GetEntry());
    bool const teacher = creature->GetEntry() == chain.teacher;
    if (teacher && Open(player,chain,chain.firstQuest) &&
        player->GetQuestStatus(chain.firstQuest) == QUEST_STATUS_INCOMPLETE)
    {
        View view;
        if (TeacherView(player->GetMapId(),player->GetInstanceId(),creature->GetObjectGuid(),view))
        {
            if (view.request.guid != player->GetGUIDLow()) { menu.SetMenuId(chain.firstMenu+34); return; }
            if (view.phase == HsHunterPhase::Choosing)
            {
                for (uint32 i = 0; i < 3; ++i)
                    Option(menu,kChoices[view.request.chain][view.request.round][i],sender,chain.firstAction+8+view.request.round*3+i);
                menu.SetMenuId(chain.firstMenu+21+view.request.round*4); return;
            }
            if (view.phase == HsHunterPhase::Reaction)
            {
                Option(menu,kAcknowledgements[view.request.chain][view.request.round],sender,chain.firstAction+20+view.request.round);
                menu.SetMenuId(chain.firstMenu+22+view.request.round*4+view.answer); return;
            }
            menu.SetMenuId(chain.firstMenu+33); return;
        }
    }
    HsHunterForget(player->GetGUIDLow()); // Saved counters reconstruct scenes whose transient playback has ended.
    uint32 offset = teacher ? 0 : 1;
    if (player->GetQuestRewardStatus(chain.firstQuest+2)) offset = teacher ? 9 : 10;
    else if (Open(player,chain,chain.firstQuest+2))
        offset = teacher ? (player->GetQuestStatus(chain.firstQuest+2) == QUEST_STATUS_COMPLETE ? 8 : 7) : 11;
    else if (player->GetQuestRewardStatus(chain.firstQuest+1)) offset = teacher ? 6 : 11;
    else if (Open(player,chain,chain.firstQuest+1))
    {
        uint32 const step = Step(player,chain.firstQuest+1,2);
        offset = teacher ? (step == 0 ? 5 : step == 1 ? 18 : 20) : (step ? 17 : 14);
        if (player->GetQuestStatus(chain.firstQuest+1) == QUEST_STATUS_INCOMPLETE && teacher == (step == 1))
            Option(menu,kTalk[chain.alliance ? 1 : 0][step][0],sender,chain.firstAction+32+step*4);
    }
    else if (player->GetQuestRewardStatus(chain.firstQuest)) offset = teacher ? 3 : 1;
    else if (teacher && Open(player,chain,chain.firstQuest))
    {
        uint32 const step = Step(player,chain.firstQuest,3);
        offset = step == 3 ? 4 : 2;
        if (step < 3 && player->GetQuestStatus(chain.firstQuest) == QUEST_STATUS_INCOMPLETE)
            Option(menu,chain.alliance ? "Show me the next set." : "Tell me about your next three.",sender,chain.firstAction+step);
    }
    menu.SetMenuId(chain.firstMenu+offset);
}
bool HsHunterHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender)
{
    HsHunterChain const* decodedChain = nullptr;
    for (auto const& chain : kHsHunterChains)
        if (action >= chain.firstAction && action < chain.firstAction+64) decodedChain = &chain;
    if (!decodedChain) return false;
    if (!player || !player->PlayerTalkClass) return true;
    auto const& chain = *decodedChain;
    auto const reply = HsHunterDecode(chain,action);
    auto& menu = player->PlayerTalkClass->GetGossipMenu();
    bool offered = false;
    for (uint32 i = 0; i < menu.MenuItemCount(); ++i)
        offered |= menu.MenuItemSender(i) == sender && menu.MenuItemAction(i) == action;
    bool const audition = reply.kind != HsHunterAction::Talk;
    uint32 const quest = chain.firstQuest + (audition ? 0 : 1);
    uint32 const step = Step(player,quest,audition ? 3 : 2);
    bool const teacher = creature && creature->GetEntry() == chain.teacher;
    View view;
    bool const own = OwnView(player->GetGUIDLow(),view);
    bool const matching = own && creature && view.request.teacher == creature->GetObjectGuid() &&
        view.request.chain == (chain.alliance ? 1u : 0u) && view.request.round == step &&
        view.request.account == (player->GetSession() ? player->GetSession()->GetAccountId() : 0) &&
        view.request.map == player->GetMapId() && view.request.instance == player->GetInstanceId();
    bool const eligible = creature && HsHunterChainFor(creature->GetEntry()) == &chain && Eligible(player,chain) &&
        Interaction(player,creature) && Open(player,chain,quest) && player->GetQuestStatus(quest) == QUEST_STATUS_INCOMPLETE;
    if (eligible && offered && matching && reply.kind == HsHunterAction::Choose &&
        (view.phase == HsHunterPhase::Expired || (view.phase == HsHunterPhase::Choosing &&
            !ActorsPresent(view.actors,player->GetMap(),player->GetGUIDLow()))))
    {
        HsHunterForget(player->GetGUIDLow()); Retry(player,creature,chain,step,sender,true); return true;
    }
    if (!eligible || !HsHunterReplyAllowed(reply,offered,teacher,audition,step,
        matching,own ? view.phase : HsHunterPhase::Queued))
    {
        HsHunterForget(player->GetGUIDLow());
        player->PlayerTalkClass->ClearMenus(); player->PlayerTalkClass->CloseGossip(); return true;
    }
    if (reply.kind == HsHunterAction::Talk)
    {
        bool const last = reply.page + 1 == (step ? 2u : 3u);
        player->PlayerTalkClass->ClearMenus();
        if (last) { player->KilledMonsterCredit(chain.firstCredit+3+step); player->PrepareQuestMenu(creature->GetObjectGuid()); }
        else Option(player->PlayerTalkClass->GetGossipMenu(),kTalk[chain.alliance ? 1 : 0][step][reply.page+1],
            sender,action+1);
        Page(player,creature,chain,(step ? 18 : 14)+reply.page+1);
    }
    else if (reply.kind == HsHunterAction::Launch)
    {
        bool const started = Start(player,creature,chain,step,sender);
        player->PlayerTalkClass->ClearMenus();
        Page(player,creature,chain,started ? 33 : 34);
    }
    else if (reply.kind == HsHunterAction::Choose)
    {
        if (ChangePhase(view.request,HsHunterPhase::Choosing,HsHunterPhase::AnswerQueued))
        {
            player->PlayerTalkClass->ClearMenus();
            std::lock_guard<std::mutex> lock(g_mutex);
            g_commands.push_back({view.request,CommandKind::Answer,reply.page});
        }
    }
    else
    {
        if (step == 2) HsHunterForget(player->GetGUIDLow());
        player->PlayerTalkClass->ClearMenus();
        player->KilledMonsterCredit(chain.firstCredit+step);
        if (step+1 < 3)
        {
            Continue(view.request,step+1); Page(player,creature,chain,33);
        }
        else { player->PrepareQuestMenu(creature->GetObjectGuid()); Page(player,creature,chain,4); }
    }
    return true;
}
void HsHunterAfterQuestChange(Player* player)
{
    if (!player) return;
    if (player->IsInWorld()) player->GetCamera().UpdateVisibilityForOwner();
    View view;
    if (OwnView(player->GetGUIDLow(),view) &&
        player->GetQuestStatus(kHsHunterChains[view.request.chain].firstQuest) != QUEST_STATUS_INCOMPLETE)
        HsHunterForget(player->GetGUIDLow());
    if (!player->PlayerTalkClass) return;
    auto& menu = player->PlayerTalkClass->GetGossipMenu();
    for (uint32 i = 0; i < menu.MenuItemCount(); ++i)
        for (auto const& chain : kHsHunterChains)
        {
            auto const reply = HsHunterDecode(chain,menu.MenuItemAction(i));
            if (reply.kind == HsHunterAction::Invalid ||
                player->GetQuestStatus(chain.firstQuest+(reply.kind == HsHunterAction::Talk ? 1 : 0)) != QUEST_STATUS_NONE) continue;
            player->PlayerTalkClass->ClearMenus(); player->PlayerTalkClass->CloseGossip(); return;
        }
}
void HsHunterUpdate(uint32_t diff)
{
    std::vector<Command> commands;
    { std::lock_guard<std::mutex> lock(g_mutex); commands.swap(g_commands); }
    for (auto const& command : commands)
    {
        auto const& request = command.request;
        auto it = g_scenes.find(request.guid);
        if (command.kind == CommandKind::Cancel)
        {
            if (it != g_scenes.end() && it->second.request.token == request.token)
            {
                if (it->second.captured) BeginCleanup(it->second,false,command.answer);
                else { RemoveActors(it->second); RemoveView(request); g_scenes.erase(it); }
            }
            else RemoveView(request);
            continue;
        }
        View view;
        if (!OwnView(request.guid,view) || view.request.token != request.token) continue;
        if (command.kind == CommandKind::Start)
        {
            if (it != g_scenes.end()) { RemoveActors(it->second); g_scenes.erase(it); }
            g_scenes.emplace(request.guid,Scene{request}); continue;
        }
        Map* map = sMapMgr.FindMap(request.map,request.instance);
        Player* player = map ? map->GetPlayer(ObjectGuid(HIGHGUID_PLAYER,request.guid)) : nullptr;
        Creature* teacher = map ? map->GetCreature(request.teacher) : nullptr;
        if (it == g_scenes.end() || !Valid(it->second,player,teacher)) continue;
        if (!ActorsPresent(it->second.actors,map,request.guid) || HsHunterVisualExpired(it->second.elapsed))
        {
            BeginCleanup(it->second,false,1); continue;
        }
        RemoveActors(it->second);
        if (!ChangePhase(request,HsHunterPhase::AnswerQueued,HsHunterPhase::Reaction)) continue;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto current = g_views.find(request.guid);
            if (current != g_views.end() && current->second.request.token == request.token) current->second.answer = command.answer;
        }
        it->second.elapsed = 0;
        auto const& chain = kHsHunterChains[request.chain];
        player->PlayerTalkClass->ClearMenus();
        Option(player->PlayerTalkClass->GetGossipMenu(),kAcknowledgements[request.chain][request.round],request.sender,
            chain.firstAction+20+request.round);
        Page(player,teacher,chain,22+request.round*4+command.answer);
    }
    for (auto it = g_scenes.begin(); it != g_scenes.end();)
    {
        auto& scene = it->second;
        Map* map = sMapMgr.FindMap(scene.request.map,scene.request.instance);
        Player* player = map ? map->GetPlayer(ObjectGuid(HIGHGUID_PLAYER,scene.request.guid)) : nullptr;
        Creature* teacher = map ? map->GetCreature(scene.request.teacher) : nullptr;
        View view;
        if (scene.cancelling)
        {
            if (Cleanup(scene,teacher,player,diff)) it = g_scenes.erase(it);
            else ++it;
            continue;
        }
        if (!Valid(scene,player,teacher) || !OwnView(scene.request.guid,view))
        {
            if (scene.captured) { BeginCleanup(scene); ++it; }
            else { RemoveActors(scene); RemoveView(scene.request); it = g_scenes.erase(it); }
            continue;
        }
        if (scene.pending)
        {
            // Defer insertion one update so a previous trio's queued removal can finish.
            scene.pending = false; ++it; continue;
        }
        if (view.phase == HsHunterPhase::Queued || view.phase == HsHunterPhase::Presenting)
        {
            if (!Present(scene,player,teacher,diff))
            {
                BeginCleanup(scene,false,2); ++it; continue;
            }
            ++it; continue; // Count the lifetime from successful spawn, not the queue.
        }
        if (view.phase == HsHunterPhase::Choosing)
        {
            scene.elapsed += std::min(diff,kHsHunterProjectionLifetime-scene.elapsed);
            bool const present = ActorsPresent(scene.actors,map,scene.request.guid);
            if (!present || HsHunterVisualExpired(scene.elapsed))
            {
                BeginCleanup(scene,true);
            }
        }
        if (view.phase == HsHunterPhase::Reaction)
        {
            // Escape has no packet in 1.12. Bound the teacher reservation when a reply is left open.
            scene.elapsed += std::min(diff,90000u-scene.elapsed);
            if (scene.elapsed == 90000) BeginCleanup(scene);
        }
        ++it;
    }
}
