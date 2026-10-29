/*
 * mod-hearthstone: database glue of the alternative hero portraits, see HsHeroPortraitStore.h.
 */

#include "HsHeroPortraitStore.h"

#include "Database/DatabaseEnv.h"
#include "Log.h"

#include <memory>
#include <vector>

namespace
{
    std::string EscapeText(std::string const& text)
    {
        std::string s = text;
        CharacterDatabase.escape_string(s);
        return s;
    }

    bool Commit(std::vector<std::string> const& statements, char const* what)
    {
        if (!CharacterDatabase.BeginTransaction())
        {
            sLog.outError("Hearthstone: BeginTransaction failed for the hero portrait %s", what);
            return false;
        }
        for (std::string const& sql : statements)
        {
            if (!CharacterDatabase.Execute(sql.c_str()))
            {
                CharacterDatabase.RollbackTransaction();
                sLog.outError("Hearthstone: could not queue a statement of the hero portrait %s", what);
                return false;
            }
        }
        if (!CharacterDatabase.CommitTransactionDirect())
        {
            sLog.outError("Hearthstone: commit failed for the hero portrait %s (character migration "
                "0021_hearthstone_hero_portraits.sql applied? see the database log)", what);
            return false;
        }
        return true;
    }

    // A scalar SELECT always returns one row, so a null result is a failed query.
    bool ReadCount(std::string const& sql, uint64_t& out)
    {
        std::unique_ptr<QueryResult> result(CharacterDatabase.Query(sql.c_str()));
        if (!result)
            return false;
        out = result->Fetch()[0].GetUInt64();
        return true;
    }
}

bool HsHeroDbStore::Load(uint32_t accountId, Hs::Heroes::HeroState& out)
{
    out = Hs::Heroes::HeroState();
    uint64_t credits = 0;
    if (!ReadCount(Hs::Heroes::CreditCountSql(accountId), credits))
    {
        sLog.outError("Hearthstone: cannot read the hero portraits of account %u "
            "(character migration 0021_hearthstone_hero_portraits.sql not applied, or database error)", accountId);
        return false;
    }
    out.credits = static_cast<uint32_t>(credits);

    // Null here means no rows: the table was proven to exist by the count above.
    std::unique_ptr<QueryResult> owned(CharacterDatabase.Query(Hs::Heroes::OwnedSelectSql(accountId).c_str()));
    if (owned)
        do
            out.owned.insert(owned->Fetch()[0].GetCppString());
        while (owned->NextRow());
    std::unique_ptr<QueryResult> choices(CharacterDatabase.Query(Hs::Heroes::ChoiceSelectSql(accountId).c_str()));
    if (choices)
        do
        {
            Field* f = choices->Fetch();
            out.selected[f[0].GetCppString()] = f[1].GetCppString();
        } while (choices->NextRow());
    return true;
}

bool HsHeroDbStore::FindClaim(uint32_t accountId, std::string const& key, bool& found, std::string& portraitId)
{
    std::unique_ptr<QueryResult> claimed(CharacterDatabase.Query(
        Hs::Heroes::ClaimLookupSql(accountId, key, EscapeText).c_str()));
    found = claimed != nullptr; // no row is a key never used: the tables were read by Load just before
    portraitId = claimed ? claimed->Fetch()[0].GetCppString() : std::string();
    return true;
}

bool HsHeroDbStore::Claim(uint32_t accountId, std::string const& key, Hs::Heroes::Portrait const& portrait)
{
    return Commit({ Hs::Heroes::CreditClaimSql(accountId, key, portrait, EscapeText),
                    Hs::Heroes::PortraitUnlockSql(accountId, portrait, "legend1") }, "unlock");
}

bool HsHeroDbStore::SaveChoice(uint32_t accountId, std::string const& heroClass, Hs::Heroes::Portrait const* portrait)
{
    return Commit({ portrait ? Hs::Heroes::ChoiceSetSql(accountId, *portrait)
                             : Hs::Heroes::ChoiceClearSql(accountId, heroClass) }, "choice");
}

bool HsHeroReady()
{
    // Asked on every inbound frame: read once. Migrations are applied at server start.
    static int ready = -1;
    if (ready < 0)
    {
        uint64_t rows = 0;
        ready = ReadCount(Hs::Heroes::CreditCountSql(0), rows) ? 1 : 0;
    }
    return ready == 1;
}

namespace
{
    int g_league = -1; // -1: not read yet; the ladder glue calls from one thread under its mutex
}

uint32_t HsHeroCurrentLeague()
{
    if (g_league < 0)
    {
        uint64_t league = 0;
        g_league = ReadCount(Hs::Heroes::LeagueCurrentSql(), league) ? static_cast<int>(league) : 0;
    }
    return static_cast<uint32_t>(g_league);
}

bool HsHeroStartLeague()
{
    if (!Commit({ Hs::Heroes::LeagueAdvanceSql() }, "league"))
        return false;
    g_league = -1;
    sLog.outString("Hearthstone: ladder league %u started", HsHeroCurrentLeague());
    return true;
}

bool HsHeroCommit(std::vector<std::string> const& statements, char const* what)
{
    return Commit(statements, what);
}

bool HsHeroGrantCredit(uint32_t accountId, uint32_t league)
{
    if (league == 0)
        return false;
    return Commit({ Hs::Heroes::CreditGrantSql(accountId, league) }, "credit");
}
