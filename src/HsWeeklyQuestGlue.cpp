#include "HsWeeklyQuestGlue.h"
#include "quests/HsWeeklyQuests.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "WeeklyQuestRotation.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
    struct Battle
    {
        uint32 matchId = 0;
        uint32 account = 0;
        uint32 mask = 0;
        bool randomHard = false;
        uint64 durableId = 0;
        uint32 matchNo = 0;
    };
    struct Notice
    {
        Hs::MatchEnd end;
        Battle battle;
    };
    // Leaf lock, as in HsNaxxQuestGlue: core and database calls stay outside it.
    std::mutex g_mutex;
    std::unordered_map<uint32, Battle> g_battles;
    std::vector<Notice> g_notices;
    std::vector<uint32> g_logins;
    // The accepted mask each character had at its last quest change, so that only a lost bit costs a write.
    std::unordered_map<uint32, uint32> g_masks;
    // The Battlegrounds weeklies: their own draw, offered by the two hosts, credited when a game ends.
    struct BgNotice
    {
        uint32 guid;
        uint32 account;
        Hs::BgWeeklyGame game;
        uint32 tries = 0;   // world updates it waited for the character to be in the world
    };
    constexpr uint32 kBgNoticeTries = 1200;
    std::vector<BgNotice> g_bgNotices;
    constexpr uint32 kBgGivers[] = {920200u, 920201u};
    constexpr uint32 kStreakQuest = 919991;
    constexpr uint32 kStreakCredit = 920030;
    constexpr uint32 kGivers[] = {919501u, 919502u};

    uint32 Bit(uint32 quest)
    {
        for (uint32 i = 0; i < std::size(Hs::kWeeklyQuests); ++i)
            if (Hs::kWeeklyQuests[i].id == quest)
                return 1u << i;
        return 0;
    }

    bool ReadStreak(uint32 guid, uint32& wins)
    {
        std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery("SELECT COALESCE("
            "(SELECT wins FROM hearthstone_weekly_streak WHERE guid=%u),0)", guid));
        if (!result)
            return false;
        wins = std::min(3u, result->Fetch()[0].GetUInt32());
        return true;
    }

    // The part below one objective unit (damage under ten) left by earlier matches of the current acceptance.
    bool ReadCarry(uint32 guid, uint32 quest, uint32& amount)
    {
        std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery("SELECT COALESCE("
            "(SELECT amount FROM hearthstone_weekly_carry WHERE guid=%u AND quest=%u),0)", guid, quest));
        if (!result)
            return false;
        amount = result->Fetch()[0].GetUInt32();
        return true;
    }

    void SyncStreak(Player* player, uint32 wins)
    {
        uint16 const slot = player->FindQuestSlot(kStreakQuest);
        if (slot >= MAX_QUEST_LOG_SIZE)
            return;
        QuestStatusData& status = player->getQuestStatusMap()[kStreakQuest];
        uint32 const current = status.m_creatureOrGOcount[0];
        if (wins < current)
        {
            status.m_creatureOrGOcount[0] = wins;
            if (status.uState != QUEST_NEW)
                status.uState = QUEST_CHANGED;
            player->SetQuestSlotCounter(slot, 0, static_cast<uint8>(wins));
            player->IncompleteQuest(kStreakQuest);
        }
        for (uint32 n = current; n < wins; ++n)
            player->KilledMonsterCredit(kStreakCredit);
    }

    // Saves the next streak. The quest log follows now when the character is in the world, else at its login.
    void AdvanceStreak(Player* player, uint32 guid, Hs::MatchEnd const& end)
    {
        uint32 wins = 0;
        if (!ReadStreak(guid, wins))
            return;
        wins = Hs::NextWeeklyStreak(wins, end);
        if (CharacterDatabase.DirectPExecute("INSERT INTO hearthstone_weekly_streak (guid,wins) VALUES (%u,%u) "
            "ON DUPLICATE KEY UPDATE wins=VALUES(wins)", guid, wins) && player)
            SyncStreak(player, wins);
    }

    uint32 AcceptedMask(Player* player)
    {
        uint32 mask = 0;
        for (uint32 i = 0; i < std::size(Hs::kWeeklyQuests); ++i)
            if (player->IsCurrentQuest(Hs::kWeeklyQuests[i].id))
                mask |= 1u << i;
        return mask;
    }

    // The open Battlegrounds weeklies of the character get the credit of one finished game.
    void CreditBgGame(Player* player, Hs::BgWeeklyGame const& game)
    {
        for (Hs::BgWeeklyQuest const& quest : Hs::kBgWeeklyQuests)
        {
            if (player->GetQuestStatus(quest.id) != QUEST_STATUS_INCOMPLETE)
                continue;
            uint32 const amount = std::min(quest.count, Hs::BgWeeklyCredit(quest, game));
            for (uint32 n = 0; n < amount; ++n)
                player->KilledMonsterCredit(quest.credit);
        }
    }
}

void HsWeeklyBgGameEnded(uint32_t guid, uint32_t account, Hs::BgWeeklyGame const& game)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_bgNotices.push_back({ guid, account, game });
}

void HsWeeklyMatchStarted(Player* player, uint32_t matchId, bool randomHard, uint64_t durableId,
    uint32_t matchNo, bool practice, bool resume)
{
    if (!player || !player->GetSession())
        return;
    Battle battle;
    battle.matchId = matchId;
    battle.account = player->GetSession()->GetAccountId();
    battle.mask = practice ? 0 : AcceptedMask(player);
    battle.randomHard = randomHard;
    battle.durableId = durableId;
    battle.matchNo = matchNo;
    if (durableId)
    {
        if (!resume)
        {
            if (!CharacterDatabase.DirectPExecute("INSERT INTO hearthstone_weekly_match "
                "(durable_id,match_no,guid,quest_mask) VALUES (%llu,%u,%u,%u) "
                "ON DUPLICATE KEY UPDATE guid=VALUES(guid),quest_mask=VALUES(quest_mask),consumed=0",
                static_cast<unsigned long long>(durableId), matchNo, player->GetGUIDLow(), battle.mask))
                battle.mask = 0;
        }
        else
        {
            battle.mask = 0;
            std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery("SELECT guid,quest_mask,consumed "
                "FROM hearthstone_weekly_match WHERE durable_id=%llu AND match_no=%u",
                static_cast<unsigned long long>(durableId), matchNo));
            if (result && result->Fetch()[0].GetUInt32() == player->GetGUIDLow() && !result->Fetch()[2].GetUInt32())
                battle.mask = result->Fetch()[1].GetUInt32() & AcceptedMask(player);
        }
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_battles[player->GetGUIDLow()] = battle;
}

void HsWeeklyMatchEnded(Hs::MatchEnd const& end)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    auto const battle = g_battles.find(end.guid);
    if (battle == g_battles.end() || battle->second.matchId != end.matchId)
        return;
    // An interrupted durable duel can be resumed. Leave its saved snapshot unconsumed. Any other match is over for
    // good, so walking away from it still breaks the streak.
    bool const counts = Hs::WeeklyNormalEnd(end) || end.why == "concede" ||
        (!battle->second.durableId && !Hs::WeeklyServerInterrupt(end));
    if (battle->second.account == end.meta.accountId && counts)
        g_notices.push_back({end, battle->second});
    g_battles.erase(battle);
}

void HsWeeklyRegisterRotations()
{
    // The server swaps the givers' weekly quests once per period (WeeklyQuestRotation::Update).
    WeeklyQuestRotation::Register({
        "hearthstone",
        std::vector<uint32>(std::begin(kGivers), std::end(kGivers)),
        [](uint32 quest) { return Hs::FindWeeklyQuest(quest) != nullptr; },
        [](uint64 week) {
            Hs::WeeklyDraw const draw = Hs::DrawWeeklyQuests(week);
            return std::vector<uint32>(draw.begin(), draw.end());
        } });
    WeeklyQuestRotation::Register({
        "hearthstone-battlegrounds",
        std::vector<uint32>(std::begin(kBgGivers), std::end(kBgGivers)),
        [](uint32 quest) { return Hs::FindBgWeeklyQuest(quest) != nullptr; },
        [](uint64 week) {
            Hs::BgWeeklyDraw const draw = Hs::DrawBgWeeklyQuests(week);
            return std::vector<uint32>(draw.begin(), draw.end());
        } });
}

void HsWeeklyUpdate()
{
    // World::Update calls this after MapManager::Update has joined its workers.
    std::vector<BgNotice> bgNotices;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        bgNotices.swap(g_bgNotices);
    }
    std::vector<BgNotice> retry;
    for (BgNotice& notice : bgNotices)
    {
        Player* player = ObjectAccessor::FindPlayer(ObjectGuid(HIGHGUID_PLAYER, notice.guid));
        if (!player || !player->GetSession() || player->GetSession()->GetAccountId() != notice.account)
            continue;   // logged out: the character is gone
        if (player->IsInWorld())
            CreditBgGame(player, notice.game);
        else if (++notice.tries < kBgNoticeTries)
            retry.push_back(notice);   // mid-teleport: the credit waits until the character is back in the world
    }
    if (!retry.empty())
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_bgNotices.insert(g_bgNotices.end(), retry.begin(), retry.end());
    }
    std::vector<Notice> notices;
    std::vector<uint32> logins;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        notices.swap(g_notices);
        logins.swap(g_logins);
    }
    for (uint32 guid : logins)
    {
        Player* player = ObjectAccessor::FindPlayer(ObjectGuid(HIGHGUID_PLAYER, guid));
        uint32 wins = 0;
        if (player && player->IsInWorld() && player->IsCurrentQuest(kStreakQuest) && ReadStreak(guid, wins))
            SyncStreak(player, wins);
    }
    for (Notice const& notice : notices)
    {
        Battle const& battle = notice.battle;
        uint32 const guid = notice.end.guid;
        Player* player = ObjectAccessor::FindPlayer(ObjectGuid(HIGHGUID_PLAYER, guid));
        if (!player || !player->IsInWorld() || !player->GetSession() ||
            player->GetSession()->GetAccountId() != battle.account)
        {
            // A logout ends the match after the character left the world: the saved streak still breaks.
            if (!battle.durableId && (battle.mask & Bit(kStreakQuest)))
                AdvanceStreak(nullptr, guid, notice.end);
            continue;
        }
        if (battle.durableId)
        {
            std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery("SELECT consumed FROM hearthstone_weekly_match "
                "WHERE durable_id=%llu AND match_no=%u", static_cast<unsigned long long>(battle.durableId), battle.matchNo));
            if (!result || result->Fetch()[0].GetUInt32() || !CharacterDatabase.DirectPExecute(
                "UPDATE hearthstone_weekly_match SET consumed=1 WHERE durable_id=%llu AND match_no=%u",
                static_cast<unsigned long long>(battle.durableId), battle.matchNo))
                continue;
        }
        bool const normal = Hs::WeeklyNormalEnd(notice.end);
        for (uint32 i = 0; i < std::size(Hs::kWeeklyQuests); ++i)
        {
            Hs::WeeklyQuest const& quest = Hs::kWeeklyQuests[i];
            if (!(battle.mask & (1u << i)) || player->GetQuestStatus(quest.id) != QUEST_STATUS_INCOMPLETE)
                continue;
            if (quest.id == kStreakQuest)
            {
                AdvanceStreak(player, guid, notice.end);
                continue;
            }
            if (!normal)
                continue;
            uint32 carry = 0;
            bool const carried = quest.unit > 1;
            if (carried && !ReadCarry(guid, quest.id, carry))
                continue;
            uint32 const before = carry;
            uint32 const amount = std::min(quest.count,
                Hs::WeeklyCredit(quest, notice.end, battle.randomHard, carried ? &carry : nullptr));
            // The carry is saved before the credit: a failure in between loses progress, it never doubles it.
            if (carried && carry != before && !CharacterDatabase.DirectPExecute("INSERT INTO hearthstone_weekly_carry "
                "(guid,quest,amount) VALUES (%u,%u,%u) ON DUPLICATE KEY UPDATE amount=VALUES(amount)", guid, quest.id, carry))
                continue;
            for (uint32 n = 0; n < amount; ++n)
                player->KilledMonsterCredit(quest.credit);
        }
    }
}

void HsWeeklyAfterQuestChange(Player* player, uint32_t acceptedQuest)
{
    if (!player)
        return;
    uint32 const guid = player->GetGUIDLow();
    uint32 const mask = AcceptedMask(player);
    uint32 removed = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto const known = g_masks.find(guid);
        // The first change after a login does not know the old mask: any bit may have gone.
        removed = (known == g_masks.end() ? ~0u : known->second) & ~mask;
        g_masks[guid] = mask;
        auto battle = g_battles.find(guid);
        if (battle != g_battles.end())
            battle->second.mask &= mask;
        for (Notice& notice : g_notices)
            if (notice.end.guid == guid)
                notice.battle.mask &= mask;
    }
    if (removed)
        CharacterDatabase.DirectPExecute("UPDATE hearthstone_weekly_match SET quest_mask=quest_mask & %u WHERE guid=%u", mask, guid);
    // Saved progress lives as long as its acceptance: an abandon or a reward removes it, a new accept starts empty.
    QuestStatusData const* status = acceptedQuest ? player->GetQuestStatusData(acceptedQuest) : nullptr;
    bool const fresh = status && (mask & Bit(acceptedQuest)) && !status->m_creatureOrGOcount[0];
    if ((removed & Bit(kStreakQuest)) || (fresh && acceptedQuest == kStreakQuest))
        CharacterDatabase.DirectPExecute("DELETE FROM hearthstone_weekly_streak WHERE guid=%u", guid);
    for (Hs::WeeklyQuest const& quest : Hs::kWeeklyQuests)
        if (quest.unit > 1 && ((removed & Bit(quest.id)) || (fresh && acceptedQuest == quest.id)))
            CharacterDatabase.DirectPExecute("DELETE FROM hearthstone_weekly_carry WHERE guid=%u AND quest=%u", guid, quest.id);
}

void HsWeeklyLogin(Player* player)
{
    if (!player)
        return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_logins.push_back(player->GetGUIDLow());
}

void HsWeeklyForget(uint32_t guid)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_battles.erase(guid);
    g_masks.erase(guid);
    // Accepted snapshots of a paused durable duel stay in the database.
}
