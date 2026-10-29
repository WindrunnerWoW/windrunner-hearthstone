#include "HsMatchStore.h"

#include "Database/DatabaseEnv.h"
#include "Log.h"

#include "HsDataset.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    // Cleared by CheckHsMatchHistoryTable when the table is missing: no insert is attempted then.
    std::atomic<bool> g_historyTable{true};

    std::string Escaped(std::string s)
    {
        CharacterDatabase.escape_string(s);
        return s;
    }

    std::string TextOf(Field const& f)
    {
        return f.IsNULL() ? std::string() : f.GetCppString();
    }

    int NumberOf(Field const& f)
    {
        return f.IsNULL() ? 0 : static_cast<int>(f.GetUInt32());
    }
}

std::shared_ptr<Hs::Match::CardDb const> LoadHsMatchCardDb()
{
    std::unique_ptr<QueryResult> rows(WorldDatabase.PQuery(
        "SELECT `id`, `name`, `card_class`, `rarity`, `race`, `text`, `card_type`, `cost`, `attack`, `health`, "
        "`durability`, `spell_damage`, `mechanics`, `overload` FROM `hearthstone_card_template` WHERE `dataset_id` = '%s'",
        Hs::kDatasetId));
    if (!rows)
    {
        sLog.outError("Hearthstone: no hearthstone_card_template rows for dataset %s in the world database; "
            "card matches are UNAVAILABLE until the world migration is applied and the server restarted",
            Hs::kDatasetId);
        return nullptr;
    }

    auto db = std::make_shared<Hs::Match::CardDb>();
    do
    {
        Field* f = rows->Fetch();
        Hs::Match::CardDef def;
        def.id = f[0].GetCppString();
        def.name = TextOf(f[1]);
        def.cardClass = TextOf(f[2]);
        def.rarity = TextOf(f[3]);
        def.race = TextOf(f[4]);
        def.text = TextOf(f[5]);
        def.type = Hs::Match::ParseCardType(TextOf(f[6]));
        def.cost = NumberOf(f[7]);
        def.attack = NumberOf(f[8]);
        def.health = NumberOf(f[9]);
        def.durability = NumberOf(f[10]);
        def.spellDamage = NumberOf(f[11]);
        Hs::Match::ApplyMechanics(TextOf(f[12]), def);
        def.overload = NumberOf(f[13]);
        db->Add(def);
    } while (rows->NextRow());

    for (std::string const& id : Hs::Match::EngineCardIds())
        if (!db->Find(id))
            sLog.outError("Hearthstone: the match engine card %s is missing from hearthstone_card_template; "
                "matches cannot start", id.c_str());

    sLog.outString("Hearthstone: match engine loaded %u cards, %u supported", static_cast<uint32>(db->Size()),
        static_cast<uint32>(db->SupportedCount()));
    return db;
}

bool LoadHsLibraryRows(std::vector<Hs::LibraryDeckRow>& out)
{
    out.clear();
    std::unique_ptr<QueryResult> table(WorldDatabase.PQuery("SHOW TABLES LIKE 'hearthstone_deck_template'"));
    if (!table)
    {
        sLog.outError("Hearthstone: table hearthstone_deck_template is missing in the world database "
            "(migration 0002_hearthstone_opponents.sql not applied? check Database.AutoUpdate.*): no opponent decks");
        return false;
    }
    // Added kind and finder (the migration rebuilds the table, so both exist once it ran).
    std::unique_ptr<QueryResult> rows(WorldDatabase.PQuery(
        "SELECT `deck_key`, `name`, `deck_class`, `cards`, `kind`, `finder` FROM `hearthstone_deck_template` ORDER BY `deck_key`"));
    if (!rows)
        return true;
    do
    {
        Field* f = rows->Fetch();
        Hs::LibraryDeckRow row;
        row.key = TextOf(f[0]);
        row.name = TextOf(f[1]);
        row.deckClass = TextOf(f[2]);
        row.cards = TextOf(f[3]);
        row.kind = TextOf(f[4]);
        row.finder = TextOf(f[5]);
        out.push_back(row);
    } while (rows->NextRow());
    return true;
}

bool LoadHsFinderEntries(std::vector<uint32_t>& out)
{
    out.clear();
    std::unique_ptr<QueryResult> table(WorldDatabase.PQuery("SHOW TABLES LIKE 'hearthstone_finder'"));
    if (!table)
    {
        sLog.outError("Hearthstone: table hearthstone_finder is missing in the world database "
            "(migration 0003_hearthstone_finder.sql not applied? check Database.AutoUpdate.*): no match-finder");
        return false;
    }
    std::unique_ptr<QueryResult> rows(WorldDatabase.PQuery(
        "SELECT `creature_entry` FROM `hearthstone_finder` ORDER BY `creature_entry`"));
    if (!rows)
        return true;
    do
    {
        out.push_back(rows->Fetch()[0].GetUInt32());
    } while (rows->NextRow());
    return true;
}

bool LoadHsFinderNames(std::vector<Hs::FinderName>& out)
{
    out.clear();
    std::unique_ptr<QueryResult> table(WorldDatabase.PQuery("SHOW TABLES LIKE 'hearthstone_finder_npc'"));
    if (!table)
    {
        sLog.outError("Hearthstone: table hearthstone_finder_npc is missing in the world database "
            "(migration 0003_hearthstone_finder.sql not applied?): match-finder opponents use the finder's own name");
        return false;
    }
    // Rows whose creature does not exist are dropped by the join (and counted in the log by the caller).
    std::unique_ptr<QueryResult> rows(WorldDatabase.PQuery(
        "SELECT n.`creature_entry`, t.`name`, n.`faction` FROM `hearthstone_finder_npc` n "
        "JOIN `creature_template` t ON t.`entry` = n.`creature_entry` WHERE n.`enabled` <> 0 ORDER BY n.`creature_entry`"));
    if (!rows)
        return true;
    do
    {
        Field* f = rows->Fetch();
        Hs::FinderName name;
        name.entry = f[0].GetUInt32();
        name.name = TextOf(f[1]);
        name.faction = TextOf(f[2]);
        out.push_back(name);
    } while (rows->NextRow());
    return true;
}

bool LoadHsOpponentRows(std::vector<Hs::OpponentRow>& out)
{
    out.clear();
    std::unique_ptr<QueryResult> table(WorldDatabase.PQuery("SHOW TABLES LIKE 'hearthstone_opponent'"));
    if (!table)
    {
        sLog.outError("Hearthstone: table hearthstone_opponent is missing in the world database "
            "(migration 0002_hearthstone_opponents.sql not applied? check Database.AutoUpdate.*): only the "
            "Hearthstone.Match.Opponents config opponents exist");
        return false;
    }
    std::unique_ptr<QueryResult> rows(WorldDatabase.PQuery(
        "SELECT `creature_entry`, `difficulty`, `deck_key`, `hero_health`, `hero_power`, `hero_card`, `hero_armor` "
        "FROM `hearthstone_opponent` ORDER BY `creature_entry`, `difficulty`, `deck_key`"));
    if (!rows)
        return true;
    do
    {
        Field* f = rows->Fetch();
        Hs::OpponentRow row;
        row.entry = f[0].GetUInt32();
        row.difficulty = TextOf(f[1]);
        row.deckKey = TextOf(f[2]);
        row.heroHealth = NumberOf(f[3]);
        row.heroPower = TextOf(f[4]);
        row.heroCard = TextOf(f[5]);
        row.heroArmor = NumberOf(f[6]);
        out.push_back(row);
    } while (rows->NextRow());
    return true;
}

bool LoadHsNpcUnlockRows(std::vector<Hs::NpcUnlockRow>& out)
{
    out.clear();
    std::unique_ptr<QueryResult> table(WorldDatabase.PQuery("SHOW TABLES LIKE 'hearthstone_npc_unlock'"));
    if (!table)
    {
        sLog.outError("Hearthstone: table hearthstone_npc_unlock is missing in the world database "
            "(migration 0002_hearthstone_opponents.sql not applied?): no quest NPC opponents");
        return false;
    }
    std::unique_ptr<QueryResult> rows(WorldDatabase.PQuery(
        "SELECT `creature_entry`, `quest_id`, `deck_key`, `faction` FROM `hearthstone_npc_unlock` ORDER BY `creature_entry`"));
    if (!rows)
        return true;
    do
    {
        Field* f = rows->Fetch();
        Hs::NpcUnlockRow row;
        row.entry = f[0].GetUInt32();
        row.questId = f[1].GetUInt32();
        row.deckKey = TextOf(f[2]);
        row.faction = TextOf(f[3]);
        out.push_back(row);
    } while (rows->NextRow());
    return true;
}

bool LoadHsRewardedQuests(uint32_t accountId, std::vector<uint32_t> const& questIds, std::set<uint32_t>& out)
{
    out.clear();
    if (questIds.empty())
        return true;
    std::string list;
    for (uint32_t id : questIds)
        list += (list.empty() ? "" : ",") + std::to_string(id);
    // A scalar head row first (see HsCollectionDbStorage::Load): null then means a failed query.
    std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
        "SELECT (SELECT COUNT(*) FROM `character_queststatus` q JOIN `characters` c ON c.`guid` = q.`guid` "
        "WHERE c.`account` = %u AND q.`rewarded` = 1 AND q.`quest` IN (%s))", accountId, list.c_str()));
    if (!head)
    {
        sLog.outError("Hearthstone: cannot read the rewarded quests of account %u", accountId);
        return false;
    }
    if (head->Fetch()[0].GetUInt64() == 0)
        return true;
    std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery(
        "SELECT DISTINCT q.`quest` FROM `character_queststatus` q JOIN `characters` c ON c.`guid` = q.`guid` "
        "WHERE c.`account` = %u AND q.`rewarded` = 1 AND q.`quest` IN (%s)", accountId, list.c_str()));
    if (!rows)
    {
        sLog.outError("Hearthstone: cannot read the rewarded quest rows of account %u", accountId);
        return false;
    }
    do
    {
        out.insert(rows->Fetch()[0].GetUInt32());
    } while (rows->NextRow());
    return true;
}

namespace
{
    class HsNpcUnlockDbStorage : public Hs::NpcUnlockStorage
    {
    public:
        bool Load(uint32_t accountId, std::map<uint32_t, uint32_t>& out) override
        {
            out.clear();
            std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
                "SELECT (SELECT COUNT(*) FROM `hearthstone_account_npc` WHERE `account_id` = %u)", accountId));
            if (!head)
            {
                sLog.outError("Hearthstone: cannot read the quest NPC unlocks of account %u "
                    "(character migration 0006_hearthstone_npc_unlocks.sql not applied, or database error)", accountId);
                return false;
            }
            if (head->Fetch()[0].GetUInt64() == 0)
                return true;
            std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery(
                "SELECT `creature_entry`, `quest_id` FROM `hearthstone_account_npc` WHERE `account_id` = %u", accountId));
            if (!rows)
                return false;
            do
            {
                Field* f = rows->Fetch();
                out[f[0].GetUInt32()] = f[1].GetUInt32();
            } while (rows->NextRow());
            return true;
        }

        bool Commit(uint32_t accountId, uint32_t entry, uint32_t questId, std::string const& sourceRaw) override
        {
            std::string source = sourceRaw.size() > 64 ? sourceRaw.substr(0, 64) : sourceRaw;
            CharacterDatabase.escape_string(source);
            // Synchronous and idempotent: an unlock that exists already is kept as it is.
            return CharacterDatabase.DirectPExecute(
                "INSERT IGNORE INTO `hearthstone_account_npc` (`account_id`, `creature_entry`, `quest_id`, `source`) "
                "VALUES (%u, %u, %u, '%s')", accountId, entry, questId, source.c_str());
        }
    };
}

void CheckHsNpcUnlockTable()
{
    std::unique_ptr<QueryResult> found(CharacterDatabase.PQuery("SHOW TABLES LIKE 'hearthstone_account_npc'"));
    if (!found)
        sLog.outError("Hearthstone: table hearthstone_account_npc is missing in the character database "
            "(migration 0006_hearthstone_npc_unlocks.sql not applied? check Database.AutoUpdate.*): quest NPCs stay locked");
}

Hs::NpcUnlockService& HsNpcUnlocks()
{
    static HsNpcUnlockDbStorage storage;
    static Hs::NpcUnlockService service(storage);
    return service;
}

Hs::MatchService& HsMatches()
{
    // Match ids start at a time based value so that an id never repeats across a restart (a stale
    // client request for an old match cannot hit a new one): seconds modulo a million, times a
    // thousand, stays below 10^9.
    static Hs::MatchService service(static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count() %
        1000000) * 1000u + 1u);
    return service;
}

void CheckHsMatchHistoryTable()
{
    std::unique_ptr<QueryResult> found(CharacterDatabase.PQuery("SHOW TABLES LIKE 'hearthstone_match_history'"));
    if (!found)
    {
        g_historyTable.store(false, std::memory_order_relaxed);
        sLog.outError("Hearthstone: table hearthstone_match_history is missing in the character database "
            "(migration 0004_hearthstone_match_history.sql not applied? check Database.AutoUpdate.*): matches "
            "are not recorded until the table exists and the server is restarted");
    }
}

bool WriteHsMatchHistory(Hs::MatchHistoryRow const& r)
{
    if (!g_historyTable.load(std::memory_order_relaxed))
        return false;
    // PExecute outside BeginTransaction on this thread is queued to the database thread (Database::Execute):
    // it neither blocks nor reports a later SQL error to the caller (the database layer logs it).
    bool const queued = CharacterDatabase.PExecute(
        "INSERT INTO `hearthstone_match_history` (`account_id`, `character_guid`, `character_name`, `match_id`, "
        "`opponent_entry`, `opponent_name`, `difficulty`, `deck_id`, `deck_name`, `deck_class`, `opponent_deck_key`, "
        "`opponent_class`, `result`, `end_reason`, `turns`, `started_at`, `ended_at`, `duration_sec`, `seed`, "
        "`dataset_id`, `dataset_hash`, `rules_version`, `ai_delay_ms`, `human_cards`, `opponent_cards`, "
        "`currency_paid`) VALUES (%u, %u, '%s', %u, %u, '%s', '%s', %u, '%s', '%s', '%s', '%s', '%s', '%s', %u, "
        "FROM_UNIXTIME(%llu), FROM_UNIXTIME(%llu), %u, %u, '%s', '%s', %u, %u, '%s', '%s', %u)",
        r.accountId, r.characterGuid, Escaped(r.characterName).c_str(), r.matchId, r.opponentEntry,
        Escaped(r.opponentName).c_str(), Escaped(r.difficulty).c_str(), r.deckId, Escaped(r.deckName).c_str(),
        Escaped(r.deckClass).c_str(), Escaped(r.opponentDeckKey).c_str(), Escaped(r.opponentClass).c_str(),
        Escaped(r.result).c_str(), Escaped(r.endReason).c_str(), r.turns,
        static_cast<unsigned long long>(r.startedAt > 0 ? r.startedAt : 0),
        static_cast<unsigned long long>(r.endedAt > 0 ? r.endedAt : 0), r.durationSec, r.seed,
        Escaped(r.datasetId).c_str(), Escaped(r.datasetHash).c_str(), r.rulesVersion, r.aiDelayMs,
        Escaped(r.humanCards).c_str(), Escaped(r.opponentCards).c_str(), r.currencyPaid);
    if (!queued)
        sLog.outError("Hearthstone: could not queue the match history row of match #%u (guid %u)", r.matchId,
            r.characterGuid);
    return queued;
}

bool LoadHsMatchHistory(uint32_t accountId, uint32_t guid, uint32_t limit, std::vector<Hs::MatchHistoryListing>& out)
{
    out.clear();
    std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery(
        "SELECT `id`, DATE_FORMAT(`ended_at`, '%%Y-%%m-%%d %%H:%%i'), `character_name`, `opponent_name`, `difficulty`, "
        "`deck_name`, `deck_class`, `opponent_deck_key`, `result`, `end_reason`, `turns`, `duration_sec`, `seed` "
        "FROM `hearthstone_match_history` WHERE `account_id` = %u AND (%u = 0 OR `character_guid` = %u) "
        "ORDER BY `id` DESC LIMIT %u", accountId, guid, guid, limit));
    if (!rows)
    {
        // No row is a normal answer (the query returns null for an empty result), so tell a missing table apart.
        std::unique_ptr<QueryResult> found(CharacterDatabase.PQuery("SHOW TABLES LIKE 'hearthstone_match_history'"));
        if (!found)
        {
            sLog.outError("Hearthstone: cannot read the match history (migration 0004_hearthstone_match_history.sql "
                "not applied, or database error)");
            return false;
        }
        return true;
    }
    do
    {
        Field* f = rows->Fetch();
        Hs::MatchHistoryListing entry;
        entry.id = f[0].GetUInt64();
        entry.endedText = TextOf(f[1]);
        entry.row.characterName = TextOf(f[2]);
        entry.row.opponentName = TextOf(f[3]);
        entry.row.difficulty = TextOf(f[4]);
        entry.row.deckName = TextOf(f[5]);
        entry.row.deckClass = TextOf(f[6]);
        entry.row.opponentDeckKey = TextOf(f[7]);
        entry.row.result = TextOf(f[8]);
        entry.row.endReason = TextOf(f[9]);
        entry.row.turns = f[10].GetUInt32();
        entry.row.durationSec = f[11].GetUInt32();
        entry.row.seed = f[12].GetUInt32();
        out.push_back(std::move(entry));
    } while (rows->NextRow());
    return true;
}
