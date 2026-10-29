#include "HsNaxxQuestGlue.h"
#include "Camera.h"
#include "Creature.h"
#include "GossipDef.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    // Gossip actions of the challenge options (sender 0x4853, block 0x48530401-0x4853041d): first + the row of the
    // challenge's quest in kNaxxQuests (1..29).
    constexpr uint32 kChallengeActionFirst = 0x48530400u;
    constexpr uint32 kChallengeActionLast = kChallengeActionFirst + 29u;

    // The challenges that a stand-in fights: the quest table says which are challenges, HsEncounters which boss each one fights.
    std::vector<Hs::NaxxQuest const*> ChallengesAt(uint32 entry)
    {
        return Hs::NaxxChallengesAtEntry(entry, [](std::string const& key) {
            Hs::Challenge const* challenge = Hs::FindChallenge(key);
            return challenge ? challenge->encounterKey : std::string();
        });
    }

    bool Incomplete(Player const* player, uint32 questId)
    {
        return player && player->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE;
    }

    // Hides a boss stand-in from every player it is not currently for. Other creatures are untouched.
    class StandInVisibility final : public AllCreatureScript
    {
    public:
        StandInVisibility() : AllCreatureScript("hearthstone_naxx_stand_in_visibility") {}

        bool CanSeeCreature(Player const* player, Creature const* creature) const override
        {
            uint32 const entry = creature->GetEntry();
            Hs::NaxxQuest const* bossQuest = Hs::NaxxBossQuestOfEntry(entry);
            if (!bossQuest)
                return true;
            bool challengeOpen = false;
            for (Hs::NaxxQuest const* quest : ChallengesAt(entry))
                challengeOpen = challengeOpen || Incomplete(player, quest->questId);
            return Hs::NaxxStandInVisible(Incomplete(player, bossQuest->questId), challengeOpen);
        }
    };

    // One running match against a stand-in, per character.
    struct Battle
    {
        uint32 matchId = 0;
        uint32 accountId = 0;
        uint32 entry = 0;
        uint32 questId = 0;      // the one quest this match can credit
        bool questOpen = false;  // it was incomplete when the match started
    };
    struct Notice
    {
        uint32 guid = 0;
        Battle battle;
    };
    // Leaf lock: never call a core or match-service API while holding it.
    std::mutex g_mutex;
    std::unordered_map<uint32, Battle> g_battles;
    std::vector<Notice> g_notices;
}

// ---- visibility --------------------------------------------------------------------------------------------

void AddHsNaxxQuestScripts()
{
    new StandInVisibility();
}

void HsNaxxRefreshVisibility(Player* player)
{
    if (player && player->IsInWorld())
        player->GetCamera().UpdateVisibilityForOwner();
}

// ---- boss stand-ins and class challenges (gossip) ----------------------------------------------------------

bool HsNaxxOpponentAvailable(Player* player, uint32_t entry)
{
    Hs::NaxxQuest const* quest = Hs::NaxxBossQuestOfEntry(entry);
    if (!quest)
        return true;
    return player && Hs::NaxxBossDuelAllowed(Incomplete(player, quest->questId));
}

bool HsNaxxChallengeGossipActive(Player* player, Creature* creature)
{
    if (!player || !creature)
        return false;
    for (Hs::NaxxQuest const* quest : ChallengesAt(creature->GetEntry()))
        if (Incomplete(player, quest->questId))
            return true;
    return false;
}

void HsNaxxAppendChallengeGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender)
{
    if (!player || !creature)
        return;
    for (Hs::NaxxQuest const* quest : ChallengesAt(creature->GetEntry()))
    {
        Hs::Challenge const* challenge = Hs::FindChallenge(quest->key);
        if (!challenge || !Incomplete(player, quest->questId) || menu.MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS)
            continue;
        menu.AddMenuItem(GOSSIP_ICON_BATTLE, "Begin the " + Hs::NaxxClassLabel(challenge->heroClass) + " class challenge.",
            sender, kChallengeActionFirst + quest->row, "", false);
        // The core reads the parallel action data vector unchecked: keep it aligned.
        menu.AddGossipMenuItemData(0, 0, 0);
    }
}

bool HsNaxxIsChallengeAction(uint32_t action)
{
    return action > kChallengeActionFirst && action <= kChallengeActionLast;
}

Hs::Challenge const* HsNaxxChallengeOfAction(Player* player, Creature* creature, uint32_t action)
{
    if (!player || !creature || !HsNaxxIsChallengeAction(action))
        return nullptr;
    uint32 const row = action - kChallengeActionFirst;
    for (Hs::NaxxQuest const* quest : ChallengesAt(creature->GetEntry()))
        if (quest->row == row)
        {
            Hs::Challenge const* challenge = Hs::FindChallenge(quest->key);
            return challenge && Incomplete(player, quest->questId) ? challenge : nullptr;
        }
    return nullptr;
}

bool HsNaxxChallengeAvailable(Player* player, uint32_t entry, Hs::Challenge const& challenge)
{
    Hs::NaxxBoss const* boss = Hs::NaxxBossByEntry(entry);
    Hs::NaxxQuest const* quest = Hs::NaxxQuestByKey(challenge.key);
    return boss && quest && quest->kind == Hs::NaxxKind::Challenge && challenge.encounterKey == boss->encounterKey &&
        Incomplete(player, quest->questId);
}

// ---- credit ------------------------------------------------------------------------------------------------

void HsNaxxMatchStarted(Player* player, Creature* creature, uint32_t matchId, Hs::Challenge const* challenge)
{
    if (!player || !player->GetSession() || !creature || !Hs::NaxxBossByEntry(creature->GetEntry()))
        return;
    // A normal boss match can credit only the boss quest, a challenge match only its own challenge quest.
    Hs::NaxxQuest const* quest = challenge ? Hs::NaxxQuestByKey(challenge->key) : Hs::NaxxBossQuestOfEntry(creature->GetEntry());
    if (!quest)
        return;
    Battle battle;
    battle.matchId = matchId;
    battle.accountId = player->GetSession()->GetAccountId();
    battle.entry = creature->GetEntry();
    battle.questId = quest->questId;
    battle.questOpen = Incomplete(player, quest->questId);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_battles[player->GetGUIDLow()] = battle;
}

void HsNaxxMatchEnded(Hs::MatchEnd const& end)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    auto const it = g_battles.find(end.guid);
    if (it == g_battles.end() || it->second.matchId != end.matchId)
        return;
    if (Hs::NaxxShouldCredit(it->second.questOpen, end.res, end.why) && it->second.entry == end.meta.opponentEntry &&
        it->second.accountId == end.meta.accountId)
        g_notices.push_back({ end.guid, it->second });
    g_battles.erase(it);
}

void HsNaxxUpdate()
{
    std::vector<Notice> notices;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        notices.swap(g_notices);
    }
    for (Notice const& notice : notices)
    {
        Player* player = ObjectAccessor::FindPlayer(ObjectGuid(HIGHGUID_PLAYER, notice.guid));
        if (!player || !player->IsInWorld() || !player->GetSession() ||
            player->GetSession()->GetAccountId() != notice.battle.accountId)
            continue;
        // Credit belongs to a quest accepted before this match. A win in another match never counts.
        if (notice.battle.questOpen && Incomplete(player, notice.battle.questId))
        {
            player->AreaExploredOrEventHappens(notice.battle.questId);
            sLog.outString("Hearthstone: %s (guid %u) beat the Naxxramas stand-in %u: quest %u credited",
                player->GetName(), notice.guid, notice.battle.entry, notice.battle.questId);
            HsNaxxRefreshVisibility(player);
        }
    }
}

void HsNaxxForget(uint32_t guid)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_battles.erase(guid);
    g_notices.erase(std::remove_if(g_notices.begin(), g_notices.end(),
        [guid](Notice const& notice) { return notice.guid == guid; }), g_notices.end());
}

void HsNaxxAfterQuestChange(Player* player)
{
    if (!player)
        return;
    uint32 const guid = player->GetGUIDLow();
    std::vector<uint32> quests;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto const battle = g_battles.find(guid);
        if (battle != g_battles.end())
            quests.push_back(battle->second.questId);
        for (Notice const& notice : g_notices)
            if (notice.guid == guid)
                quests.push_back(notice.battle.questId);
    }
    // Status is read outside the leaf lock. Reaccepting later starts a fresh quest that needs a fresh match.
    for (uint32 quest : quests)
    {
        if (player->GetQuestStatus(quest) != QUEST_STATUS_NONE)
            continue;
        std::lock_guard<std::mutex> lock(g_mutex);
        auto const battle = g_battles.find(guid);
        if (battle != g_battles.end() && battle->second.questId == quest)
            battle->second.questOpen = false;
        for (Notice& notice : g_notices)
            if (notice.guid == guid && notice.battle.questId == quest)
                notice.battle.questOpen = false;
    }
}
