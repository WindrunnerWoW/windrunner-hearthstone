#include "HsHordePaladinQuest.h"
#include "quests/HsHordePaladinRules.h"
#include "Creature.h"
#include "GossipDef.h"
#include "Player.h"
#include "Map.h"
#include "MapManager.h"
#include "Spell.h"
#include "Opcodes.h"
#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "WorldSession.h"

namespace
{
    uint32 Lesson(Player* player)
    {
        for (uint32 quest : { kHsHordePaladinResearch, kHsHordePaladinRehearsal })
        {
            auto const status = player->GetQuestStatus(quest);
            if (HsHordePaladinLessonAllowed(player->GetTeam() == HORDE, player->GetLevel(),
                player->GetQuestRewardStatus(919704),
                status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE,
                player->GetQuestRewardStatus(quest),
                player->GetQuestRewardStatus(quest == kHsHordePaladinResearch ? 919704 : kHsHordePaladinResearch)))
                return quest;
        }
        return 0;
    }

    uint32 Step(Player* player, uint32 quest)
    {
        auto const* status = player->GetQuestStatusData(quest);
        return status ? HsHordePaladinNextStep({ status->m_creatureOrGOcount[0],
            status->m_creatureOrGOcount[1] }) : 2;
    }

    void Option(GossipMenu& menu, char const* label, uint32 sender, uint32 action)
    {
        if (menu.MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS) return;
        menu.AddMenuItem(GOSSIP_ICON_CHAT, label, sender, action, "", false);
        menu.AddGossipMenuItemData(0, 0, 0);
    }


    struct Request
    {
        uint32 guid;
        uint32 account;
        uint32 map;
        uint32 instance;
        ObjectGuid teacher;
        uint64 token = 0;
        uint32 objective = 3;
    };
    enum class Phase { Waiting, Ready, ReplyQueued };
    struct View { Request request; Phase phase = Phase::Waiting; };
    struct Command { Request request; bool protest; };
    struct Cancel { uint32 guid; uint64 token; };
    struct Scene
    {
        Request request;
        HsHordePaladinSceneClock clock;
        bool firstTick = true;
        bool responding = false;
        HsHordePaladinReadingClock reading;
    };
    // Hooks only enqueue/copy transient views. World update owns scenes and all RP.
    // Leaf lock: no core API calls while locked. Tokens distinguish retry attempts.
    std::mutex g_mutex;
    uint64 g_token = 0;
    std::vector<Command> g_commands;
    std::vector<Cancel> g_cancels;
    std::unordered_map<uint32, View> g_views;
    std::unordered_map<uint32, Scene> g_scenes;

    bool SceneView(uint32 guid, View& view)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto const it = g_views.find(guid);
        if (it == g_views.end()) return false;
        view = it->second;
        return true;
    }
    bool Current(Request const& request)
    {
        View view;
        return SceneView(request.guid, view) && view.request.token == request.token;
    }
    void RemoveView(Request const& request)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto const it = g_views.find(request.guid);
        if (it != g_views.end() && it->second.request.token == request.token) g_views.erase(it);
    }
    void ReadyView(Request const& request)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_views.find(request.guid);
        if (it != g_views.end() && it->second.request.token == request.token && it->second.phase == Phase::Waiting)
            it->second.phase = Phase::Ready;
    }
    bool EnqueueScene(Request request, bool protest)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_views.find(request.guid);
        if (protest)
        {
            if (it == g_views.end() || it->second.request.token != request.token || it->second.phase != Phase::Ready)
                return false;
            it->second.phase = Phase::ReplyQueued;
        }
        else
        {
            if (it != g_views.end()) return false; // Duplicate readiness never restarts the timer.
            request.token = ++g_token;
            g_views.emplace(request.guid, View{request, Phase::Waiting});
        }
        g_commands.push_back({request, protest});
        return true;
    }
    bool Valid(Request const& request, Player* player, Creature* teacher)
    {
        uint32 const quest = HsHordePaladinSceneQuest(request.objective);
        uint32 const step = request.objective < 2 ? request.objective : 1;
        return quest && player && teacher && player->IsInWorld() && player->GetSession() &&
            player->GetSession()->GetAccountId() == request.account &&
            player->GetMapId() == request.map && player->GetInstanceId() == request.instance &&
            teacher->GetObjectGuid() == request.teacher && teacher->GetEntry() == kHsHordePaladin &&
            player->IsAlive() && teacher->IsAlive() &&
            player->IsWithinDistInMap(teacher, 25.0f) && player->IsWithinLOSInMap(teacher) &&
            Lesson(player) == quest && Step(player, quest) == step &&
            player->GetQuestStatus(quest) == QUEST_STATUS_INCOMPLETE;
    }
    void HolyLight(Player* player, Creature* teacher)
    {
        // Native Holy Light rank 1 hit visual. Wire animation only: no actual heal,
        // aura, radius, damage, mana use or cooldown; the NPC targets herself.
        constexpr uint32 spell = 635;
        SpellCastTargets targets;
        targets.setUnitTarget(teacher);
        WorldPacket packet(SMSG_SPELL_GO, 64);
        packet << teacher->GetPackGUID() << teacher->GetPackGUID() << spell << uint16(CAST_FLAG_UNKNOWN9);
        packet << uint8(1) << teacher->GetObjectGuid() << uint8(0) << targets;
        player->GetSession()->SendPacket(&packet);
    }

    struct ReadingLine { char const* text; uint32 pause; };
    ReadingLine const kLibram[] = {
        { "I shall read the passage aloud. The heading is 'On Blessings, Humility, and the Evident Superiority of Those Who Bestow Them.' An encouraging start.", 8500 },
        { "'Attend, faithful servant, for the Light does not distribute its favor without purpose. It has chosen you to lift the lowly, steady the wavering, and look magnificent while doing so.'", 10000 },
        { "'Let no lesser soul mistake your radiance for vanity. Your polished armor is a lantern of redemption, and those who complain of the glare have plainly grown accustomed to darkness.'", 11000 },
        { "'When your companion falters, bless their arm with might. Strength is a sacred gift, especially when bestowed by someone whose judgment is beyond reasonable dispute.'", 11500 },
        { "'Should danger approach, place your ally beneath the Light's protection. The righteous shield the vulnerable; they do not abandon them merely because their footwear disappoints.'", 11000 },
        { "'Offer redemption even to the erring. Explain their errors patiently, in full, and at sufficient volume that nearby witnesses may benefit from your considerable moral clarity.'", 10000 },
        { "'Then advance together, each humble soldier strengthened by your blessing. Let justice guide your hand, let mercy temper your wrath, and let everyone remember who made this possible.'", 10500 },
        { "'Thus shall the smallest servant stand tall beneath the Light.' There is a footnote about polishing. Of course there is. Even the footnote has a footnote about polishing.", 9000 },
        { "Useful finding: blessings strengthen allies, protection keeps them standing. The remaining seven pages appear to strengthen the author's opinion of himself. I shall record only the useful part.", 8000 },
    };
    ReadingLine const kAccounts[] = {
        { "An Alliance eyewitness account. I shall read it aloud. 'At dawn our banners rose, and every heart knew that honor, duty, and the Light marched beside us.'", 8000 },
        { "'Before the threatened bridge stood our noble champion. His armor shone like the morning sun; his bearing recalled a king; his immaculate cloak recalled a considerably wealthier king.'", 11500 },
        { "'He spoke of justice, of the innocent, and of redemption for all who would embrace the Light. We listened in reverent silence. The enemy, lacking proper manners, continued advancing.'", 11500 },
        { "'Our newest recruit trembled at the line. The champion blessed his sword arm, and courage returned. Shoulder beside shoulder, our little band held firm against the charge.'", 11000 },
        { "'When a wounded soldier stumbled, our champion shielded him beneath a holy blessing. His comrades drew him to safety while the others guarded the bridge. None stood alone that day.'", 11500 },
        { "'Then, radiant beyond description, the champion raised his hammer...' Beyond description? The next page describes it. Every stud. Every gleaming edge. I appear to have mistaken a catalog for evidence.", 11500 },
        { "'And his cloak streamed nobly in the wind...' Yes, the cloak again. How stirring. I wonder whether the enemy has reached the bridge yet. I certainly wish to reach the next paragraph.", 9000 },
        { "'At last we prevailed, united in honor, redeemed by courage, and bathed in the Light of...' I am skipping the sunset. It receives two pages and contributes nothing to the defense of a bridge.", 9500 },
        { "There. Allies held the line; blessings strengthened them; protection saved the wounded. Useful tactics, buried beneath a heroic quantity of tailoring. If another cloak appears, I am closing the book.", 8000 },
    };
    ReadingLine const* Reading(uint32 objective, uint32& count)
    {
        count = objective == 0 ? sizeof(kLibram) / sizeof(kLibram[0]) : sizeof(kAccounts) / sizeof(kAccounts[0]);
        return objective == 0 ? kLibram : kAccounts;
    }

    struct Exchange
    {
        char const* opening;
        uint32 texts[3];
        char const* replies[2];
    };
    Exchange const kExchanges[] = {
        { "Let's examine the libram.", { 9195261, 9195262, 9195263 },
            { "Please, read the passage on blessings.", "I'll record what the blessing does." } },
        { "Let's read the Alliance accounts.", { 9195264, 9195265, 9195266 },
            { "Please, read their account of the battle.", "I'll compare the tactics, too." } },
        { "I'll help lay out your practice cards.", { 9195267, 9195268, 9195269 },
            { "I'm ready. Let's lay out the cards.", "I'll put the recruits beside the blessings." } },
        { "I'm ready to watch your demonstration.", { 9195270, 9195271, 9195272 },
            { "I'm ready. Show me a turn.", "I don't think that's how it works." } },
    };
}

bool HsHordePaladinGossipActive(Player* player, Creature* creature)
{
    return player && creature && creature->GetEntry() == kHsHordePaladin && Lesson(player) != 0;
}

void HsHordePaladinAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender)
{
    if (!HsHordePaladinGossipActive(player, creature)) return;
    uint32 const quest = Lesson(player);
    uint32 const step = Step(player, quest);
    // Reopen completed acknowledgements from native progress, including COMPLETE
    // but unrewarded quests. There is no saved conversation cache.
    menu.SetMenuId(quest == kHsHordePaladinResearch
        ? (step == 2 ? 65021 : step == 1 ? 65023 : 65020)
        : (step == 2 ? 65022 : step == 1 ? 65024 : 65020));
    if (step == 2 || player->GetQuestStatus(quest) != QUEST_STATUS_INCOMPLETE) return;
    uint32 const index = step + (quest == kHsHordePaladinRehearsal ? 2 : 0);
    View view;
    if (SceneView(player->GetGUIDLow(), view))
    {
        // Core greeting prepared native quest entries before this hook.
        // Readings have no interactive middle page; only the demonstration has a protest.
        player->PlayerTalkClass->ClearMenus();
        bool const ready = view.request.objective == 3 && view.phase == Phase::Ready &&
            Valid(view.request, player, creature);
        menu.SetMenuId(ready ? 65025 : 65026);
        if (ready) Option(menu, kExchanges[3].replies[1], sender, kHsHordePaladinActionLast);
        return;
    }
    Option(menu, kExchanges[index].opening, sender, kHsHordePaladinActionFirst + index * 3);
}

bool HsHordePaladinHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender)
{
    if (action < kHsHordePaladinActionFirst || action > kHsHordePaladinActionLast) return false;
    if (!player || !player->PlayerTalkClass) return true;
    if (!HsHordePaladinGossipActive(player, creature) || !player->GetSession() || !player->IsInWorld() ||
        !player->IsAlive() || !creature->IsAlive() ||
        player->GetSession()->GetCurrentGossipGUID() != creature->GetObjectGuid() ||
        !player->IsWithinDistInMap(creature, INTERACTION_DISTANCE) || !player->IsWithinLOSInMap(creature))
    {
        player->PlayerTalkClass->CloseGossip();
        return true;
    }
    uint32 const quest = Lesson(player);
    uint32 const step = Step(player, quest);
    uint32 const index = step + (quest == kHsHordePaladinRehearsal ? 2 : 0);
    auto& menu = player->PlayerTalkClass->GetGossipMenu();
    bool offered = false;
    for (uint32 i = 0; i < menu.MenuItemCount(); ++i)
        offered |= menu.MenuItemSender(i) == sender && menu.MenuItemAction(i) == action;
    if (step == 2 || player->GetQuestStatus(quest) != QUEST_STATUS_INCOMPLETE ||
        !HsHordePaladinActionAllowed(index, action, offered))
    {
        player->PlayerTalkClass->CloseGossip();
        return true;
    }
    uint32 const page = (action - kHsHordePaladinActionFirst) % 3;
    if (index < 2)
    {
        View view;
        if (!HsHordePaladinReadingActionAllowed(page, SceneView(player->GetGUIDLow(), view)))
        {
            player->PlayerTalkClass->CloseGossip();
            return true;
        }
        if (page == 1)
        {
            EnqueueScene(Request{ player->GetGUIDLow(), player->GetSession()->GetAccountId(),
                player->GetMapId(), player->GetInstanceId(), creature->GetObjectGuid(), 0, index }, false);
            player->PlayerTalkClass->CloseGossip();
            return true;
        }
    }
    if (index == 3)
    {
        View view;
        bool const active = SceneView(player->GetGUIDLow(), view);
        bool const ready = active && view.phase == Phase::Ready && Valid(view.request, player, creature);
        if (!HsHordePaladinDemonstrationActionAllowed(page, active, ready))
        {
            player->PlayerTalkClass->CloseGossip();
            return true;
        }
        if (page != 0)
        {
            Request const request = page == 2 ? view.request : Request{ player->GetGUIDLow(),
                player->GetSession()->GetAccountId(), player->GetMapId(), player->GetInstanceId(),
                creature->GetObjectGuid(), 0 };
            EnqueueScene(request, page == 2);
            player->PlayerTalkClass->CloseGossip();
            return true;
        }
    }
    player->PlayerTalkClass->ClearMenus();
    if (page < 2)
        Option(player->PlayerTalkClass->GetGossipMenu(), kExchanges[index].replies[page], sender, action + 1);
    else
    {
        // Save only at the final acknowledgement, before displaying the ending.
        // Closing/relogging after this cannot repeat the objective.
        player->KilledMonsterCredit(kHsHordePaladinCredits[index]);
        player->PrepareQuestMenu(creature->GetObjectGuid());
        HsHordePaladinAppendGossip(player, creature, player->PlayerTalkClass->GetGossipMenu(), sender);
    }
    player->PlayerTalkClass->SendGossipMenu(kExchanges[index].texts[page], creature->GetObjectGuid());
    return true;
}

bool HsHordePaladinCanInteractPacket(Player* player, WorldPacket const& packet)
{
    uint32 const opcode = packet.GetOpcode();
    bool const gossip = opcode == CMSG_GOSSIP_HELLO || opcode == CMSG_GOSSIP_SELECT_OPTION;
    switch (opcode)
    {
        case CMSG_GOSSIP_HELLO:
        case CMSG_GOSSIP_SELECT_OPTION:
        case CMSG_QUESTGIVER_HELLO:
        case CMSG_QUESTGIVER_QUERY_QUEST:
        case CMSG_QUESTGIVER_ACCEPT_QUEST:
        case CMSG_QUESTGIVER_COMPLETE_QUEST:
        case CMSG_QUESTGIVER_REQUEST_REWARD:
        case CMSG_QUESTGIVER_CHOOSE_REWARD:
            break;
        default: return true; // Status queries and abandoning remain normal core actions.
    }
    if (!player || !player->IsInWorld() || packet.size() < sizeof(uint64)) return true;
    View view;
    if (!SceneView(player->GetGUIDLow(), view)) return true;
    WorldPacket copy(packet);
    copy.rpos(0);
    ObjectGuid target;
    copy >> target;
    bool const allowed = HsHordePaladinInteractionAllowed(target == view.request.teacher,
        view.phase == Phase::Ready, gossip);
    if (!allowed && player->PlayerTalkClass)
        player->PlayerTalkClass->CloseGossip();
    return allowed;
}

void HsHordePaladinAfterQuestChange(Player* player)
{
    View view;
    if (player && SceneView(player->GetGUIDLow(), view) &&
        player->GetQuestStatus(HsHordePaladinSceneQuest(view.request.objective)) == QUEST_STATUS_NONE)
        HsHordePaladinForget(player->GetGUIDLow());
}

void HsHordePaladinForget(uint32_t guid)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_views.erase(guid);
    g_commands.erase(std::remove_if(g_commands.begin(), g_commands.end(),
        [guid](Command const& command) { return command.request.guid == guid; }), g_commands.end());
    g_cancels.push_back({guid, ++g_token});
}

void HsHordePaladinUpdate(uint32_t diff)
{
    std::vector<Command> commands;
    std::vector<Cancel> cancels;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        commands.swap(g_commands);
        cancels.swap(g_cancels);
    }
    for (Cancel const& cancel : cancels)
    {
        auto it = g_scenes.find(cancel.guid);
        if (it != g_scenes.end() && it->second.request.token < cancel.token)
        {
            RemoveView(it->second.request);
            g_scenes.erase(it);
        }
    }
    for (Command const& command : commands)
    {
        Request const& request = command.request;
        Map* map = sMapMgr.FindMap(request.map, request.instance);
        Player* player = map ? map->GetPlayer(ObjectGuid(HIGHGUID_PLAYER, request.guid)) : nullptr;
        Creature* teacher = map ? map->GetCreature(request.teacher) : nullptr;
        if (!Current(request) || !Valid(request, player, teacher))
        {
            RemoveView(request);
            continue;
        }
        auto it = g_scenes.find(request.guid);
        if (!command.protest)
        {
            // A newer retry may replace an invalidated attempt; identical starts never reset it.
            if (it != g_scenes.end() && it->second.request.token == request.token) continue;
            if (it != g_scenes.end()) g_scenes.erase(it);
            g_scenes.emplace(request.guid, Scene{request, {}, true});
            if (request.objective < 2)
            {
                uint32 count;
                teacher->MonsterSayToPlayer(Reading(request.objective, count)[0].text, player);
            }
            else
            {
                teacher->MonsterYell("THE LIGHT COMMANDS ME TO PLAY THIS.", LANG_UNIVERSAL, player);
                HolyLight(player, teacher);
            }
        }
        else if (it != g_scenes.end() && it->second.request.token == request.token && it->second.clock.Ready())
        {
            teacher->MonsterYell("Silence.", LANG_UNIVERSAL, player);
            // A fresh five-second pause starts at this shout, not at selection or the preceding tick.
            it->second.clock = {};
            it->second.firstTick = true;
            it->second.responding = true;
        }
        else
            RemoveView(request);
    }
    for (auto it = g_scenes.begin(); it != g_scenes.end();)
    {
        Scene& scene = it->second;
        Map* map = sMapMgr.FindMap(scene.request.map, scene.request.instance);
        Player* player = map ? map->GetPlayer(ObjectGuid(HIGHGUID_PLAYER, it->first)) : nullptr;
        Creature* teacher = map ? map->GetCreature(scene.request.teacher) : nullptr;
        if (!Current(scene.request) || !Valid(scene.request, player, teacher))
        {
            RemoveView(scene.request);
            it = g_scenes.erase(it);
            continue;
        }
        if (scene.request.objective < 2)
        {
            uint32 count;
            ReadingLine const* lines = Reading(scene.request.objective, count);
            bool const advance = scene.reading.Advance(scene.firstTick ? 0 : diff,
                lines[scene.reading.line].pause, count);
            scene.firstTick = false;
            if (advance)
            {
                if (scene.reading.Done(count))
                {
                    // The final passage has played and its reading pause has elapsed.
                    // No later gossip action can bypass this native objective boundary.
                    player->KilledMonsterCredit(kHsHordePaladinCredits[scene.request.objective]);
                    RemoveView(scene.request);
                    it = g_scenes.erase(it);
                    continue;
                }
                teacher->MonsterSayToPlayer(lines[scene.reading.line].text, player);
            }
            ++it;
            continue;
        }
        // Never count the world tick preceding the first actual shout/effect.
        bool const ready = scene.clock.Advance(scene.firstTick ? 0 : diff);
        scene.firstTick = false;
        if (ready && scene.responding)
        {
            teacher->MonsterYell("I AM A PALADIIIIIIIIIN, LIGHT AND JUSTICE AND SHINYNESS", LANG_UNIVERSAL, player);
            HolyLight(player, teacher);
            // Save completion only after the final RP shout/effect actually plays.
            player->KilledMonsterCredit(kHsHordePaladinCredits[3]);
            RemoveView(scene.request);
            it = g_scenes.erase(it);
            continue;
        }
        if (ready) ReadyView(scene.request);
        ++it;
    }
}
