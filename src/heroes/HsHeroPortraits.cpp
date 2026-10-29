#include "HsHeroPortraits.h"
#include <algorithm>

namespace Hs
{
namespace Heroes
{
    namespace
    {
        std::string U(uint32_t value) { return std::to_string(value); }
        std::string Quote(SqlEscape const& escape, std::string const& text) { return "'" + escape(text) + "'"; }
    }

    Portrait const* FindPortrait(std::string const& id)
    {
        for (Portrait const& portrait : kPortraits)
            if (id == portrait.id)
                return &portrait;
        return nullptr;
    }

    Portrait const* DefaultPortrait(std::string const& heroClass)
    {
        for (Portrait const& portrait : kPortraits)
            if (portrait.isDefault && heroClass == portrait.heroClass)
                return &portrait;
        return nullptr;
    }

    bool IsClass(std::string const& heroClass) { return DefaultPortrait(heroClass) != nullptr; }

    bool ValidKey(std::string const& key)
    {
        return key.size() >= 12 && key.size() <= 16 &&
            std::all_of(key.begin(), key.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z'); });
    }

    char const* RefusalWhy(Refusal refusal)
    {
        switch (refusal)
        {
            case Refusal::Class: return "class";
            case Refusal::Unknown: return "unknown";
            case Refusal::Locked: return "locked";
            case Refusal::NoCredit: return "nocredit";
            case Refusal::Owned: return "owned";
            case Refusal::None: break;
        }
        return "";
    }

    Refusal ValidateSelect(std::string const& heroClass, std::string const& id, std::set<std::string> const& owned)
    {
        if (!IsClass(heroClass))
            return Refusal::Class;
        if (id == kDefaultChoice)
            return Refusal::None;
        Portrait const* portrait = FindPortrait(id);
        if (!portrait)
            return Refusal::Unknown;
        if (heroClass != portrait->heroClass)
            return Refusal::Class;
        if (portrait->isDefault)
            return Refusal::None;
        return owned.count(id) ? Refusal::None : Refusal::Locked;
    }

    Refusal ValidateUnlock(std::string const& id, std::set<std::string> const& owned, uint32_t credits)
    {
        Portrait const* portrait = FindPortrait(id);
        if (!portrait || portrait->isDefault)
            return Refusal::Unknown;
        if (owned.count(id))
            return Refusal::Owned;
        return credits == 0 ? Refusal::NoCredit : Refusal::None;
    }

    namespace
    {
        // The stored choice of a class when it is a real, owned, non-default portrait of that class.
        Portrait const* ValidChoice(HeroState const& state, std::string const& heroClass)
        {
            auto it = state.selected.find(heroClass);
            if (it == state.selected.end())
                return nullptr;
            Portrait const* chosen = FindPortrait(it->second);
            if (!chosen || chosen->isDefault || heroClass != chosen->heroClass || !state.owned.count(chosen->id))
                return nullptr;
            return chosen;
        }
    }

    std::string EffectivePortrait(HeroState const& state, std::string const& heroClass)
    {
        Portrait const* fallback = DefaultPortrait(heroClass);
        if (!fallback)
            return "";
        Portrait const* chosen = ValidChoice(state, heroClass);
        return chosen ? chosen->id : fallback->id;
    }

    std::string EncodeHeroesPayload(HeroState const& state)
    {
        std::string out = "credits=" + U(state.credits) + ";own=";
        bool first = true;
        for (std::string const& id : state.owned)
        {
            Portrait const* portrait = FindPortrait(id);
            if (!portrait || portrait->isDefault)
                continue;
            out += (first ? "" : ",") + id;
            first = false;
        }
        out += ";sel=";
        first = true;
        for (auto const& entry : state.selected)
        {
            Portrait const* chosen = ValidChoice(state, entry.first);
            if (!chosen)
                continue;
            out += (first ? "" : ",") + entry.first + "." + chosen->id;
            first = false;
        }
        return out;
    }

    std::string CreditGrantSql(uint32_t account, uint32_t league)
    {
        return std::string("INSERT IGNORE INTO `") + kTableCredit + "` (`account_id`, `league`, `earned_at`) VALUES (" +
            U(account) + ", " + U(league) + ", UNIX_TIMESTAMP())";
    }

    std::string CreditClaimSql(uint32_t account, std::string const& key, Portrait const& portrait,
                               SqlEscape const& escape)
    {
        return std::string("UPDATE `") + kTableCredit + "` SET `portrait_id` = '" + portrait.id + "', `claim_key` = " +
            Quote(escape, key) + " WHERE `account_id` = " + U(account) + " AND `claim_key` IS NULL ORDER BY `league` LIMIT 1";
    }

    std::string PortraitUnlockSql(uint32_t account, Portrait const& portrait, char const* source)
    {
        return std::string("INSERT IGNORE INTO `") + kTablePortrait +
            "` (`account_id`, `portrait_id`, `source`, `unlocked_at`) VALUES (" + U(account) + ", '" + portrait.id +
            "', '" + source + "', UNIX_TIMESTAMP())";
    }

    std::string ChoiceSetSql(uint32_t account, Portrait const& portrait)
    {
        return std::string("REPLACE INTO `") + kTableChoice + "` (`account_id`, `class`, `portrait_id`) VALUES (" +
            U(account) + ", '" + portrait.heroClass + "', '" + portrait.id + "')";
    }

    std::string ChoiceClearSql(uint32_t account, std::string const& heroClass)
    {
        // Only catalog class names are written; an unknown class matches no row.
        std::string const safe = IsClass(heroClass) ? heroClass : std::string();
        return std::string("DELETE FROM `") + kTableChoice + "` WHERE `account_id` = " + U(account) + " AND `class` = '" + safe + "'";
    }

    std::string GmCreditSql(uint32_t account)
    {
        return std::string("INSERT INTO `") + kTableCredit + "` (`account_id`, `league`, `earned_at`) SELECT " + U(account) +
            ", COALESCE(MAX(`league`) + 1, " + U(kGmLeagueBase) + "), UNIX_TIMESTAMP() FROM `" + kTableCredit +
            "` WHERE `account_id` = " + U(account) + " AND `league` >= " + U(kGmLeagueBase);
    }

    std::vector<std::string> GmRevokeSql(uint32_t account, Portrait const& portrait)
    {
        return { std::string("DELETE FROM `") + kTablePortrait + "` WHERE `account_id` = " + U(account) +
                     " AND `portrait_id` = '" + portrait.id + "'",
                 std::string("DELETE FROM `") + kTableChoice + "` WHERE `account_id` = " + U(account) +
                     " AND `portrait_id` = '" + portrait.id + "'" };
    }

    std::vector<std::string> GmResetSql(uint32_t account)
    {
        std::vector<std::string> sql;
        for (char const* table : { kTableCredit, kTablePortrait, kTableChoice })
            sql.push_back(std::string("DELETE FROM `") + table + "` WHERE `account_id` = " + U(account));
        return sql;
    }

    std::string LeagueCurrentSql()
    {
        return std::string("SELECT COALESCE(MAX(`league`), 0) FROM `") + kTableLeague + "`";
    }

    std::string LeagueAdvanceSql()
    {
        return std::string("INSERT INTO `") + kTableLeague + "` (`league`, `started_at`) SELECT COALESCE(MAX(`league`), 0) + 1, "
            "UNIX_TIMESTAMP() FROM `" + kTableLeague + "`";
    }

    std::string CreditCountSql(uint32_t account)
    {
        return std::string("SELECT COUNT(*) FROM `") + kTableCredit + "` WHERE `account_id` = " + U(account) +
            " AND `claim_key` IS NULL";
    }

    std::string OwnedSelectSql(uint32_t account)
    {
        return std::string("SELECT `portrait_id` FROM `") + kTablePortrait + "` WHERE `account_id` = " + U(account);
    }

    std::string ChoiceSelectSql(uint32_t account)
    {
        return std::string("SELECT `class`, `portrait_id` FROM `") + kTableChoice + "` WHERE `account_id` = " + U(account);
    }

    std::string ClaimLookupSql(uint32_t account, std::string const& key, SqlEscape const& escape)
    {
        return std::string("SELECT `portrait_id` FROM `") + kTableCredit + "` WHERE `account_id` = " + U(account) +
            " AND `claim_key` = " + Quote(escape, key);
    }

    HeroResult GetHeroes(HeroStore& store, uint32_t account)
    {
        HeroResult r;
        if (store.Load(account, r.state))
            r.status = HeroStatus::Ok;
        return r;
    }

    HeroResult SelectPortrait(HeroStore& store, uint32_t account, std::string const& heroClass, std::string const& id)
    {
        HeroResult r;
        if (!store.Load(account, r.state))
            return r;
        Refusal const why = ValidateSelect(heroClass, id, r.state.owned);
        if (why != Refusal::None)
        {
            r.status = HeroStatus::Refused;
            r.why = why;
            return r;
        }
        Portrait const* portrait = id == kDefaultChoice ? nullptr : FindPortrait(id);
        if (portrait && portrait->isDefault)
            portrait = nullptr; // the class default is stored as no row
        if (!store.SaveChoice(account, heroClass, portrait))
            return r;
        if (portrait)
            r.state.selected[heroClass] = portrait->id;
        else
            r.state.selected.erase(heroClass);
        r.status = HeroStatus::Ok;
        return r;
    }

    HeroResult UnlockPortrait(HeroStore& store, uint32_t account, std::string const& key, std::string const& id)
    {
        HeroResult r;
        if (!store.Load(account, r.state))
            return r;
        bool found = false;
        std::string claimed;
        if (!store.FindClaim(account, key, found, claimed))
            return r;
        if (found)
        {
            r.status = HeroStatus::Replay;
            return r;
        }
        Refusal const why = ValidateUnlock(id, r.state.owned, r.state.credits);
        if (why != Refusal::None)
        {
            r.status = HeroStatus::Refused;
            r.why = why;
            return r;
        }
        if (!store.Claim(account, key, *FindPortrait(id)))
            return r;
        r.state.owned.insert(id);
        --r.state.credits;
        r.status = HeroStatus::Ok;
        return r;
    }
}
}
