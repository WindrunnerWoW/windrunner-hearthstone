#include "../src/heroes/HsHeroPortraits.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <regex>
#include <set>
#include <sstream>

namespace
{
    int checks = 0, failed = 0;
    void Check(bool value, int line)
    {
        ++checks;
        if (!value) { ++failed; std::printf("hero portrait check failed at %d\n", line); }
    }
}
#define CHECK(value) Check((value), __LINE__)

using namespace Hs::Heroes;

int main(int argc, char** argv)
{
    auto const escape = [](std::string const& s) {
        std::string out;
        for (char c : s) { if (c == '\'' || c == '\\') out += '\\'; out += c; }
        return out;
    };
    char const* const classes[] = { "DRUID", "HUNTER", "MAGE", "PALADIN", "PRIEST", "ROGUE", "SHAMAN", "WARLOCK", "WARRIOR" };

    Portrait const* first = nullptr;
    Portrait const* second = nullptr;
    for (Portrait const& p : kPortraits)
        if (!p.isDefault && std::strcmp(p.heroClass, "MAGE") == 0)
        {
            if (!first) first = &p;
            else if (!second) second = &p;
        }

    // --sql: print the statements of one account's lifetime, one per line, for tests/validate_hero_sql.py.
    if (argc > 1 && std::strcmp(argv[1], "--sql") == 0)
    {
        std::string const k1 = "abcdef123456", k2 = "ghijkl654321";
        for (std::string const& sql : {
            CreditGrantSql(7, 1), CreditGrantSql(7, 1), CreditGrantSql(7, 2),
            CreditCountSql(7), ClaimLookupSql(7, k1, escape),
            CreditClaimSql(7, k1, *first, escape), PortraitUnlockSql(7, *first, "legend1"),
            CreditCountSql(7), ClaimLookupSql(7, k1, escape),
            CreditClaimSql(7, k2, *second, escape), PortraitUnlockSql(7, *second, "legend1"),
            CreditCountSql(7), OwnedSelectSql(7),
            ChoiceSetSql(7, *first), ChoiceSetSql(7, *second),
            ChoiceSelectSql(7), ChoiceClearSql(7, "MAGE"), ChoiceSelectSql(7),
            GmCreditSql(9), GmCreditSql(9), ChoiceSetSql(7, *second) })
            std::puts(sql.c_str());
        for (std::string const& sql : GmRevokeSql(7, *second))
            std::puts(sql.c_str());
        for (std::string const& sql : GmResetSql(7))
            std::puts(sql.c_str());
        return 0;
    }

    // Catalog: nine classes, one default each, unique short ids, the default first.
    std::set<std::string> ids;
    std::size_t total = 0;
    for (char const* cls : classes)
    {
        Portrait const* def = DefaultPortrait(cls);
        CHECK(def && def->isDefault && IsClass(cls));
        unsigned defaults = 0, count = 0;
        for (Portrait const& p : kPortraits)
            if (std::strcmp(p.heroClass, cls) == 0)
            {
                ++count;
                defaults += p.isDefault;
                CHECK(std::strlen(p.id) <= kMaxIdLen && ids.insert(p.id).second);
                CHECK(count > 1 || p.isDefault);
            }
        CHECK(defaults == 1 && count >= 2);
        total += count;
    }
    CHECK(total == std::size(kPortraits) && !IsClass("DEATHKNIGHT") && !IsClass("") && DefaultPortrait("mage") == nullptr);

    // The catalog against the JSON, when a path is given.
    if (argc > 1)
    {
        std::ifstream in(argv[1], std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        std::string const json = text.str();
        CHECK(!json.empty());
        std::regex card("\"card_id\":\\s*\"([^\"]+)\"");
        std::set<std::string> fromJson;
        for (std::sregex_iterator it(json.begin(), json.end(), card), end; it != end; ++it)
            fromJson.insert((*it)[1]);
        CHECK(fromJson == ids);
    }

    Portrait const* alt = nullptr;
    Portrait const* otherClass = nullptr;
    for (Portrait const& p : kPortraits)
    {
        if (!alt && !p.isDefault && std::strcmp(p.heroClass, "MAGE") == 0) alt = &p;
        if (!otherClass && !p.isDefault && std::strcmp(p.heroClass, "PRIEST") == 0) otherClass = &p;
    }
    CHECK(alt && otherClass);
    std::set<std::string> none, owned{ alt->id };

    // HERO_SELECT matrix.
    CHECK(ValidateSelect("MAGE", "default", none) == Refusal::None);
    CHECK(ValidateSelect("MAGE", DefaultPortrait("MAGE")->id, none) == Refusal::None);
    CHECK(ValidateSelect("MAGE", alt->id, owned) == Refusal::None);
    CHECK(ValidateSelect("MAGE", alt->id, none) == Refusal::Locked);
    CHECK(ValidateSelect("MAGE", "NOPE", owned) == Refusal::Unknown);
    CHECK(ValidateSelect("MAGE", otherClass->id, std::set<std::string>{ otherClass->id }) == Refusal::Class);
    CHECK(ValidateSelect("MAGE", DefaultPortrait("PRIEST")->id, none) == Refusal::Class);
    CHECK(ValidateSelect("DEATHKNIGHT", "default", owned) == Refusal::Class);
    CHECK(ValidateSelect("", alt->id, owned) == Refusal::Class);

    // HERO_UNLOCK matrix.
    CHECK(ValidateUnlock(alt->id, none, 1) == Refusal::None);
    CHECK(ValidateUnlock(alt->id, none, 0) == Refusal::NoCredit);
    CHECK(ValidateUnlock(alt->id, owned, 3) == Refusal::Owned);
    CHECK(ValidateUnlock("NOPE", none, 1) == Refusal::Unknown);
    CHECK(ValidateUnlock(DefaultPortrait("MAGE")->id, none, 1) == Refusal::Unknown);
    CHECK(ValidateUnlock("default", none, 1) == Refusal::Unknown);
    CHECK(std::strcmp(RefusalWhy(Refusal::NoCredit), "nocredit") == 0 && std::strcmp(RefusalWhy(Refusal::Locked), "locked") == 0 &&
          std::strcmp(RefusalWhy(Refusal::Owned), "owned") == 0 && std::strcmp(RefusalWhy(Refusal::Unknown), "unknown") == 0 &&
          std::strcmp(RefusalWhy(Refusal::Class), "class") == 0);

    // Keys.
    CHECK(ValidKey("abcdef123456") && ValidKey("0123456789abcdef"));
    CHECK(!ValidKey("ABCDEF1234567890") && !ValidKey("short") && !ValidKey("abcdef12345678901") && !ValidKey("abcdef12345'--") && !ValidKey("abcdef 1234567"));

    // Effective portrait and payload round trip.
    HeroState state;
    CHECK(EncodeHeroesPayload(state) == "credits=0;own=;sel=");
    CHECK(EffectivePortrait(state, "MAGE") == DefaultPortrait("MAGE")->id && EffectivePortrait(state, "BOGUS").empty());
    state.credits = 2;
    state.owned = { alt->id, otherClass->id };
    state.selected["MAGE"] = alt->id;
    state.selected["PRIEST"] = alt->id;       // wrong class: ignored
    state.selected["ROGUE"] = "NOPE";         // unknown: ignored
    state.selected["DRUID"] = DefaultPortrait("DRUID")->id; // default is never stored as a choice
    CHECK(EffectivePortrait(state, "MAGE") == alt->id);
    CHECK(EffectivePortrait(state, "PRIEST") == DefaultPortrait("PRIEST")->id);
    std::string const payload = EncodeHeroesPayload(state);
    CHECK(payload == std::string("credits=2;own=") + (std::string(alt->id) < otherClass->id ? alt->id : otherClass->id) + "," +
                     (std::string(alt->id) < otherClass->id ? otherClass->id : alt->id) + ";sel=MAGE." + alt->id);
    state.owned.erase(alt->id); // a revoked portrait falls back to the default
    CHECK(EffectivePortrait(state, "MAGE") == DefaultPortrait("MAGE")->id);

    // The whole pool owned and chosen stays a bounded payload (it is sent in frames of up to 240 bytes).
    HeroState full;
    for (Portrait const& p : kPortraits)
        if (!p.isDefault) { full.owned.insert(p.id); full.selected[p.heroClass] = p.id; }
    std::string const big = EncodeHeroesPayload(full);
    CHECK(big.size() < 4000 && big.find("own=") != std::string::npos);

    // SQL.
    CHECK(LeagueCurrentSql() == "SELECT COALESCE(MAX(`league`), 0) FROM `hearthstone_ladder_league`");
    CHECK(LeagueAdvanceSql() == "INSERT INTO `hearthstone_ladder_league` (`league`, `started_at`) SELECT COALESCE(MAX(`league`), 0) + 1, "
                                "UNIX_TIMESTAMP() FROM `hearthstone_ladder_league`");
    CHECK(std::string(kTableLeague) != "hearthstone_hero_credit");
    CHECK(CreditGrantSql(7, 3) ==
          "INSERT IGNORE INTO `hearthstone_hero_credit` (`account_id`, `league`, `earned_at`) VALUES (7, 3, UNIX_TIMESTAMP())");
    std::string const claim = CreditClaimSql(7, "abcdef123456", *alt, escape);
    CHECK(claim.find("`claim_key` IS NULL") != std::string::npos && claim.find("ORDER BY `league` LIMIT 1") != std::string::npos &&
          claim.find("'abcdef123456'") != std::string::npos && claim.find(alt->id) != std::string::npos);
    CHECK(CreditClaimSql(7, "a'b", *alt, escape).find("'a\\'b'") != std::string::npos);
    CHECK(PortraitUnlockSql(7, *alt, "legend1").find("INSERT IGNORE INTO `hearthstone_hero_portrait`") == 0);
    CHECK(ChoiceSetSql(7, *alt).find("REPLACE INTO `hearthstone_hero_choice`") == 0 && ChoiceSetSql(7, *alt).find("'MAGE'") != std::string::npos);
    CHECK(ChoiceClearSql(7, "MAGE") == "DELETE FROM `hearthstone_hero_choice` WHERE `account_id` = 7 AND `class` = 'MAGE'");
    CHECK(ChoiceClearSql(7, "x' OR '1'='1").find("OR") == std::string::npos);
    CHECK(CreditCountSql(7).find("`claim_key` IS NULL") != std::string::npos);
    CHECK(OwnedSelectSql(7).find("`account_id` = 7") != std::string::npos && ChoiceSelectSql(7).find("`class`") != std::string::npos);
    CHECK(ClaimLookupSql(7, "abcdef123456", escape).find("`claim_key` = 'abcdef123456'") != std::string::npos);

    // GM statements.
    CHECK(GmCreditSql(9).find("INSERT INTO `hearthstone_hero_credit`") == 0 &&
          GmCreditSql(9).find("COALESCE(MAX(`league`) + 1, 1000000)") != std::string::npos &&
          GmCreditSql(9).find("`account_id` = 9 AND `league` >= 1000000") != std::string::npos);
    auto const revoke = GmRevokeSql(7, *alt);
    CHECK(revoke.size() == 2 && revoke[0].find("DELETE FROM `hearthstone_hero_portrait`") == 0 &&
          revoke[1].find("DELETE FROM `hearthstone_hero_choice`") == 0 && revoke[1].find(alt->id) != std::string::npos);
    auto const reset = GmResetSql(7);
    CHECK(reset.size() == 3 && reset[0].find("hearthstone_hero_credit") != std::string::npos &&
          reset[1].find("hearthstone_hero_portrait") != std::string::npos && reset[2].find("hearthstone_hero_choice") != std::string::npos);

    std::printf("hero portrait tests: %d checks, %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
