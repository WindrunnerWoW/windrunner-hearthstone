/*
 * mod-hearthstone: database glue of the alternative hero portraits (reward track). The catalog, the
 * validation and the SQL text live in src/heroes (pure C++); this file only runs them against the character
 * database (data/sql/character/0021_hearthstone_hero_portraits.sql). All calls come from the world thread.
 */

#ifndef MOD_HEARTHSTONE_HS_HERO_PORTRAIT_STORE_H
#define MOD_HEARTHSTONE_HS_HERO_PORTRAIT_STORE_H

#include "heroes/HsHeroPortraits.h"

#include <cstdint>
#include <string>
#include <vector>

// The character database behind HERO_GET / HERO_SELECT / HERO_UNLOCK (Hs::Heroes::GetHeroes and friends).
class HsHeroDbStore : public Hs::Heroes::HeroStore
{
public:
    // False (logged) if the tables cannot be read, for example while migration 0021 is not applied.
    bool Load(uint32_t accountId, Hs::Heroes::HeroState& out) override;
    bool FindClaim(uint32_t accountId, std::string const& key, bool& found, std::string& portraitId) override;
    bool Claim(uint32_t accountId, std::string const& key, Hs::Heroes::Portrait const& portrait) override;
    bool SaveChoice(uint32_t accountId, std::string const& heroClass, Hs::Heroes::Portrait const* portrait) override;
};

// True when the portrait tables exist (migration 0021 applied): WELCOME lists the "heroes" capability only then.
bool HsHeroReady();

// The league whose Legend #1 reach pays a credit: the highest row of hearthstone_ladder_league (migration 0022), read once
// and kept. 0 while there is none (a ladder from before the table is not paid retroactively, and neither is a server
// without the migration).
uint32_t HsHeroCurrentLeague();

// A new league starts (a new ladder was created: the first one, or a reset). True when the row was written; false (logged)
// keeps the old number, so a missing migration never blocks a reset.
bool HsHeroStartLeague();

// One credit for the account in the league (INSERT IGNORE: a replay adds nothing). True when it was written.
bool HsHeroGrantCredit(uint32_t accountId, uint32_t league);

// Runs the statements in one transaction (the GM commands). True when it committed.
bool HsHeroCommit(std::vector<std::string> const& statements, char const* what);

// HERO_CHANGED push to the account's online character (why: legend1 or gm). Defined in HearthstoneScripts.cpp.
void HsPushHeroChanged(uint32_t accountId, std::string const& why);

#endif
