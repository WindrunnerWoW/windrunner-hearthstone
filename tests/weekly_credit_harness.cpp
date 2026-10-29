// Adapters for tests/test_weekly_quest_credit.py. The production glue is inserted verbatim below.
#include "WEEKLY_HEADER"
#include "GLUE_HEADER"
#include "ROTATION_DRAW_HEADER"
#include <algorithm>
#include <functional>
#include <list>
#include <string>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using uint64 = uint64_t;
constexpr int HIGHGUID_PLAYER = 0;
constexpr uint16 MAX_QUEST_LOG_SIZE = 25;
enum { QUEST_STATUS_NONE, QUEST_STATUS_INCOMPLETE, QUEST_STATUS_COMPLETE };
enum { QUEST_NEW, QUEST_CHANGED };
struct QuestStatusData
{
    int m_status = QUEST_STATUS_NONE;
    int uState = QUEST_NEW;
    uint32 m_creatureOrGOcount[4]{};
};
struct ObjectGuid
{
    uint32 low;
    ObjectGuid(int, uint32 guid) : low(guid) {}
};
struct Camera
{
    unsigned refreshes = 0;
    void UpdateVisibilityForOwner() { ++refreshes; }
};
struct WorldSession
{
    Player* player;
    uint32 account;
    uint32 GetAccountId() const { return account; }
    Player* GetPlayer() const { return player; }
};
class Player
{
public:
    uint32 guid;
    WorldSession session;
    bool online = true;
    Camera camera;
    std::map<uint32, QuestStatusData> quests;
    uint32 logCount = 0;
    explicit Player(uint32 id, uint32 account = 1) : guid(id), session{this, account} {}
    uint32 GetGUIDLow() const { return guid; }
    WorldSession* GetSession() { return &session; }
    bool IsInWorld() const { return online; }
    Camera& GetCamera() { return camera; }
    int GetQuestStatus(uint32 id) const
    {
        auto const it = quests.find(id);
        return it == quests.end() ? QUEST_STATUS_NONE : it->second.m_status;
    }
    bool IsCurrentQuest(uint32 id) const { return GetQuestStatus(id) != QUEST_STATUS_NONE; }
    QuestStatusData const* GetQuestStatusData(uint32 id) const
    {
        auto const it = quests.find(id);
        return it == quests.end() ? nullptr : &it->second;
    }
    auto& getQuestStatusMap() { return quests; }
    uint16 FindQuestSlot(uint32 id) const { return IsCurrentQuest(id) ? 0 : MAX_QUEST_LOG_SIZE; }
    void SetQuestSlotCounter(uint16, uint8, uint8 count) { logCount = count; }
    void IncompleteQuest(uint32 id) { quests[id].m_status = QUEST_STATUS_INCOMPLETE; }
    void KilledMonsterCredit(uint32 credit)
    {
        for (auto& row : quests)
        {
            Hs::WeeklyQuest const* q = Hs::FindWeeklyQuest(row.first);
            if (q && q->credit == credit && row.second.m_status == QUEST_STATUS_INCOMPLETE)
            {
                ++row.second.m_creatureOrGOcount[0];
                if (q->id == 919991) logCount = row.second.m_creatureOrGOcount[0];
                if (row.second.m_creatureOrGOcount[0] >= q->count)
                    row.second.m_status = QUEST_STATUS_COMPLETE;
            }
        }
    }
};
static std::map<uint32, Player*> players;
namespace ObjectAccessor
{
    Player* FindPlayer(ObjectGuid guid)
    {
        auto const it = players.find(guid.low);
        return it == players.end() ? nullptr : it->second;
    }
}
using QuestRelationsMap = std::multimap<uint32, uint32>;
static struct ObjectMgrAdapter
{
    QuestRelationsMap relations;
    QuestRelationsMap& GetCreatureQuestRelationsMap() { return relations; }
} sObjectMgr;
static uint64 testPeriod = 1791842400;
static struct WorldAdapter
{
    std::map<uint32, WorldSession*> sessions;
    time_t GetGameTime() const { return static_cast<time_t>(testPeriod); }
    auto const& GetAllSessions() const { return sessions; }
} sWorld;
struct WeeklyQuestLedger { static uint64 GetCurrentPeriod(time_t now) { return static_cast<uint64>(now); } };
static struct LogAdapter { template<class... T> void outString(char const*, T...) {} } sLog;

struct Field { uint32 value = 0; uint32 GetUInt32() const { return value; } };
struct QueryResult
{
    Field fields[3]{};
    QueryResult(uint32 a, uint32 b = 0, uint32 c = 0) : fields{{a},{b},{c}} {}
    Field* Fetch() { return fields; }
};
static struct DatabaseAdapter
{
    struct Row { uint32 guid, mask, consumed; };
    std::map<std::pair<uint64, uint32>, Row> matches;
    std::map<uint32, uint32> streaks;
    std::map<std::pair<uint32, uint32>, uint32> carries;
    unsigned maskWrites = 0;
    bool failWrites = false, failReads = false;
    static std::string Format(char const* format, va_list args)
    {
        char buffer[2048]; vsnprintf(buffer, sizeof(buffer), format, args); return buffer;
    }
    QueryResult* PQuery(char const* format, ...)
    {
        if (failReads) return nullptr;
        va_list args; va_start(args, format); std::string sql = Format(format, args); va_end(args);
        uint64 id = 0; uint32 no = 0, guid = 0, quest = 0;
        if (sql.find("hearthstone_weekly_carry") != std::string::npos)
        {
            std::sscanf(sql.c_str() + sql.find("guid="), "guid=%u AND quest=%u", &guid, &quest);
            return new QueryResult(carries[{guid, quest}]);
        }
        if (sql.find("SELECT COALESCE") == 0)
        {
            std::sscanf(sql.c_str() + sql.find("guid="), "guid=%u", &guid);
            return new QueryResult(streaks[guid]);
        }
        std::sscanf(sql.c_str() + sql.find("durable_id="), "durable_id=%llu AND match_no=%u", &id, &no);
        auto const it = matches.find({id, no});
        if (it == matches.end()) return nullptr;
        Row const row = it->second;
        return sql.find("SELECT consumed") == 0 ? new QueryResult(row.consumed) : new QueryResult(row.guid, row.mask, row.consumed);
    }
    bool DirectPExecute(char const* format, ...)
    {
        if (failWrites) return false;
        va_list args; va_start(args, format); std::string sql = Format(format, args); va_end(args);
        uint64 id = 0; uint32 no = 0, guid = 0, mask = 0, wins = 0, quest = 0;
        if (sql.find("INSERT INTO hearthstone_weekly_match") == 0)
        {
            std::sscanf(sql.c_str() + sql.find("VALUES"), "VALUES (%llu,%u,%u,%u)", &id, &no, &guid, &mask);
            matches[{id,no}] = {guid,mask,0};
        }
        else if (sql.find("UPDATE hearthstone_weekly_match SET consumed") == 0)
        {
            std::sscanf(sql.c_str() + sql.find("durable_id="), "durable_id=%llu AND match_no=%u", &id, &no);
            matches.at({id,no}).consumed = 1;
        }
        else if (sql.find("UPDATE hearthstone_weekly_match SET quest_mask") == 0)
        {
            ++maskWrites;
            std::sscanf(sql.c_str() + sql.find(" & "), " & %u WHERE guid=%u", &mask, &guid);
            for (auto& match : matches) if (match.second.guid == guid) match.second.mask &= mask;
        }
        else if (sql.find("DELETE FROM hearthstone_weekly_streak") == 0)
        {
            std::sscanf(sql.c_str() + sql.find("guid="), "guid=%u", &guid); streaks.erase(guid);
        }
        else if (sql.find("INSERT INTO hearthstone_weekly_streak") == 0)
        {
            std::sscanf(sql.c_str() + sql.find("VALUES"), "VALUES (%u,%u)", &guid, &wins); streaks[guid] = wins;
        }
        else if (sql.find("INSERT INTO hearthstone_weekly_carry") == 0)
        {
            std::sscanf(sql.c_str() + sql.find("VALUES"), "VALUES (%u,%u,%u)", &guid, &quest, &wins); carries[{guid, quest}] = wins;
        }
        else if (sql.find("DELETE FROM hearthstone_weekly_carry") == 0)
        {
            std::sscanf(sql.c_str() + sql.find("guid="), "guid=%u AND quest=%u", &guid, &quest); carries.erase({guid, quest});
        }
        else { std::printf("Unexpected SQL: %s\n", sql.c_str()); std::abort(); }
        return true;
    }
} CharacterDatabase;

// INSERT_CURRENT_GLUE

static int checks = 0, failed = 0;
#define CHECK(value) do { ++checks; if (!(value)) { ++failed; std::printf("weekly glue failed at %d: %s\n", __LINE__, #value); } } while (0)
static void Reset(Player& p)
{
    g_battles.clear(); g_notices.clear(); g_logins.clear(); g_masks.clear(); HsWeeklyRegisterRotations();
    CharacterDatabase = {}; players.clear(); sWorld.sessions.clear(); sObjectMgr.relations.clear();
    p.quests.clear(); p.online = true; p.logCount = 0;
    players[p.guid] = &p; sWorld.sessions[p.session.account] = &p.session;
}
static void Accept(Player& p, uint32 quest)
{
    p.quests[quest] = {}; p.quests[quest].m_status = QUEST_STATUS_INCOMPLETE;
    HsWeeklyAfterQuestChange(&p, quest);
}
static void Abandon(Player& p, uint32 quest)
{
    p.quests.erase(quest); HsWeeklyAfterQuestChange(&p);
}
static uint32 Count(Player& p, uint32 quest) { return p.quests[quest].m_creatureOrGOcount[0]; }
static uint32 Carry(Player& p) { auto const it = CharacterDatabase.carries.find({p.guid, 919992u}); return it == CharacterDatabase.carries.end() ? 0 : it->second; }
static bool HasCarry(Player& p) { return CharacterDatabase.carries.count({p.guid, 919992u}) != 0; }
static Hs::MatchEnd End(Player& p, uint32 match, char const* res = "win", char const* why = "hero")
{
    Hs::MatchEnd end; end.guid = p.guid; end.matchId = match; end.res = res; end.why = why;
    end.meta.accountId = p.session.account; end.meta.humanClass = "MAGE"; end.ownTurns = 8;
    end.tallies[0].turns = 8; end.tallies[0].heroDamage = 29;
    return end;
}
static void Finish(Player& p, uint32 match, char const* res = "win", char const* why = "hero")
{
    HsWeeklyMatchEnded(End(p, match, res, why)); HsWeeklyUpdate();
}
int main()
{
    Player p(100), alt(200);
    Reset(p);
    sObjectMgr.relations.emplace(919501, 123);
    sObjectMgr.relations.emplace(123, 919980);
    WeeklyQuestRotation::Update();
    CHECK(sObjectMgr.relations.count(919501) == 4 && sObjectMgr.relations.count(919502) == 3);
    CHECK(sObjectMgr.relations.count(123) == 1);
    auto relations = sObjectMgr.relations;
    WeeklyQuestRotation::Update(); CHECK(relations == sObjectMgr.relations);
    ++testPeriod; WeeklyQuestRotation::Update(); CHECK(relations == sObjectMgr.relations);
    testPeriod += 604799; WeeklyQuestRotation::Update(); CHECK(relations != sObjectMgr.relations);
    relations = sObjectMgr.relations;
    // A GM reload of creature_questrelation: the table rows (123) come back, the runtime weekly rows do not.
    for (auto it = sObjectMgr.relations.begin(); it != sObjectMgr.relations.end(); )
        it = it->first != 123 && Hs::FindWeeklyQuest(it->second) ? sObjectMgr.relations.erase(it) : std::next(it);
    WeeklyQuestRotation::Update(); CHECK(relations == sObjectMgr.relations);

    Reset(p);
    HsWeeklyMatchStarted(&p, 1); Accept(p, 919980); Finish(p, 1); CHECK(Count(p, 919980) == 0);
    for (uint32 q : {919981u,920112u,920118u,919990u,919993u}) Accept(p,q);
    HsWeeklyMatchStarted(&p, 2); Finish(p, 2);
    CHECK(Count(p,919980)==1 && Count(p,919981)==1 && Count(p,920112)==1 && Count(p,920118)==0);
    CHECK(Count(p,919990)==0 && Count(p,919993)==1);
    HsWeeklyMatchStarted(&p, 3, true); Finish(p,3); CHECK(Count(p,919990)==1);

    Reset(p); Accept(p,919980);
    HsWeeklyMatchStarted(&p, 1); Abandon(p,919980); Accept(p,919980); Finish(p,1);
    CHECK(Count(p,919980)==0);
    HsWeeklyMatchStarted(&p, 2); HsWeeklyMatchEnded(End(p,2));
    Abandon(p,919980); Accept(p,919980); HsWeeklyUpdate(); CHECK(Count(p,919980)==0);
    HsWeeklyMatchStarted(&p,3); auto bad=End(p,3); ++bad.meta.accountId;
    HsWeeklyMatchEnded(bad); HsWeeklyUpdate(); CHECK(Count(p,919980)==0);
    HsWeeklyMatchStarted(&p,4); Finish(p,5); CHECK(Count(p,919980)==0); Finish(p,4); CHECK(Count(p,919980)==1);

    Reset(p); Accept(p,919981); Accept(p,919992);
    HsWeeklyMatchStarted(&p,1); Finish(p,1,"loss"); CHECK(Count(p,919981)==1 && Count(p,919992)==2);
    CHECK(Carry(p)==9);
    HsWeeklyMatchStarted(&p,2); Finish(p,2,"draw","turns"); CHECK(Count(p,919981)==2 && Count(p,919992)==5);
    CHECK(Carry(p)==8);
    for (char const* why : {"concede","logout","stuck","teleport","death","npc","gm","error"})
    {
        HsWeeklyMatchStarted(&p,3); Finish(p,3,"loss",why); CHECK(Count(p,919981)==2 && Count(p,919992)==5);
    }
    CHECK(Carry(p)==8);
    Abandon(p,919992); CHECK(!HasCarry(p));
    CharacterDatabase.carries[{p.guid, 919992u}] = 6; Accept(p,919992); CHECK(!HasCarry(p));
    // Only a lost acceptance rewrites the paused-duel masks.
    unsigned const writes = CharacterDatabase.maskWrites;
    Accept(p,919980); CHECK(CharacterDatabase.maskWrites==writes);
    Abandon(p,919980); CHECK(CharacterDatabase.maskWrites==writes+1);

    Reset(p); Accept(p,919980);
    HsWeeklyMatchStarted(&p,1,false,77,1); g_battles.clear(); // process restart, saved acceptance survives
    Accept(p,919981);
    HsWeeklyMatchStarted(&p,2,false,77,1,false,true); Finish(p,2);
    CHECK(Count(p,919980)==1 && Count(p,919981)==0);
    HsWeeklyMatchStarted(&p,3,false,77,1,false,true); Finish(p,3); CHECK(Count(p,919980)==1);
    HsWeeklyMatchStarted(&p,4,false,77,2); g_battles.clear(); Abandon(p,919980); Accept(p,919980);
    HsWeeklyMatchStarted(&p,5,false,77,2,false,true); Finish(p,5); CHECK(Count(p,919980)==0);
    HsWeeklyMatchStarted(&p,6,false,77,3); Finish(p,6,"int","error"); CHECK(Count(p,919980)==0);
    HsWeeklyMatchStarted(&p,7,false,77,3,false,true); Finish(p,7); CHECK(Count(p,919980)==1);

    Reset(p); Accept(p,919980); HsWeeklyMatchStarted(&p,1,false,88,1); g_battles.clear();
    players[alt.guid]=&alt; Accept(alt,919980);
    HsWeeklyMatchStarted(&alt,2,false,88,1,false,true); Finish(alt,2); CHECK(Count(alt,919980)==0);

    Reset(p); Accept(p,919991);
    for (uint32 match=1; match<=2; ++match)
    {
        HsWeeklyMatchStarted(&p,match); Finish(p,match); CHECK(Count(p,919991)==match);
    }
    p.quests[919991].m_creatureOrGOcount[0]=0; HsWeeklyLogin(&p); HsWeeklyUpdate(); CHECK(Count(p,919991)==2);
    HsWeeklyMatchStarted(&p,3); Finish(p,3,"loss"); CHECK(Count(p,919991)==0 && p.logCount==0);
    HsWeeklyMatchStarted(&p,4); Finish(p,4); CHECK(Count(p,919991)==1);
    HsWeeklyMatchStarted(&p,5); Finish(p,5,"draw","turns"); CHECK(Count(p,919991)==0);
    HsWeeklyMatchStarted(&p,6); Finish(p,6);
    HsWeeklyMatchStarted(&p,7); Finish(p,7,"sur","concede"); CHECK(Count(p,919991)==0);
    for (uint32 match=8; match<=10; ++match) { HsWeeklyMatchStarted(&p,match); Finish(p,match); }
    CHECK(Count(p,919991)==3 && p.GetQuestStatus(919991)==QUEST_STATUS_COMPLETE);
    HsWeeklyMatchStarted(&p,11); Finish(p,11,"loss"); CHECK(Count(p,919991)==3);
    Abandon(p,919991); Accept(p,919991); CHECK(CharacterDatabase.streaks.count(p.guid)==0);
    HsWeeklyMatchStarted(&p,12); Finish(p,12); CHECK(Count(p,919991)==1);
    HsWeeklyMatchStarted(&p,13,false,99,1,true); Finish(p,13,"sur","concede"); CHECK(Count(p,919991)==1);
    HsWeeklyMatchStarted(&p,14,false,99,2,true); g_battles.clear();
    HsWeeklyMatchStarted(&p,15,false,99,2,true,true); Finish(p,15); CHECK(Count(p,919991)==1);
    HsWeeklyMatchStarted(&p,16); Finish(p,16,"int","gm"); CHECK(Count(p,919991)==1);
    HsWeeklyMatchStarted(&p,17); Finish(p,17,"int","teleport"); CHECK(Count(p,919991)==0);
    HsWeeklyMatchStarted(&p,18); Finish(p,18); CHECK(Count(p,919991)==1);
    // Logging out of a losing game: the match ends after the character left the world, the saved streak breaks.
    HsWeeklyMatchStarted(&p,19); HsWeeklyMatchEnded(End(p,19,"int","logout")); HsWeeklyForget(p.guid);
    p.online = false; HsWeeklyUpdate(); CHECK(CharacterDatabase.streaks[p.guid]==0);
    p.online = true; HsWeeklyLogin(&p); HsWeeklyUpdate(); CHECK(Count(p,919991)==0 && p.logCount==0);
    HsWeeklyMatchStarted(&p,20); Finish(p,20); CHECK(Count(p,919991)==1);
    HsWeeklyMatchStarted(&p,21); CharacterDatabase.failWrites=true; Finish(p,21); CHECK(Count(p,919991)==1);
    std::printf("Actual weekly glue: %d checks, %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
