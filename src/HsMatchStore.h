/*
 * mod-hearthstone: database glue of the match engine. The rules, the
 * AI and the match service live in src/match (pure C++); this file only reads
 * the card definitions from the world table hearthstone_card_template.
 */

#ifndef MOD_HEARTHSTONE_HS_MATCH_STORE_H
#define MOD_HEARTHSTONE_HS_MATCH_STORE_H

#include "match/HsMatchService.h"
#include "match/HsOpponents.h"

#include <memory>
#include <set>
#include <vector>

// Every card of Hs::kDatasetId as a Hs::Match::CardDef. Null (logged) if the table is
// missing or has no rows for the dataset. Logs how many cards the engine supports.
std::shared_ptr<Hs::Match::CardDb const> LoadHsMatchCardDb();

// Every row of hearthstone_deck_template (the library of prepared opponent decks) by deck key.
// False (logged) if the table is missing; true with no rows if it is empty.
bool LoadHsLibraryRows(std::vector<Hs::LibraryDeckRow>& out);

// Every row of hearthstone_opponent (creature entry, difficulty, deck key and the boss overrides),
// ordered by entry, difficulty and deck key. False (logged) if the table is missing.
bool LoadHsOpponentRows(std::vector<Hs::OpponentRow>& out);

// World migration 0003: the creature entries of hearthstone_finder, and the enabled names of
// hearthstone_finder_npc joined with creature_template. False (logged) if the table is missing.
bool LoadHsFinderEntries(std::vector<uint32_t>& out);
bool LoadHsFinderNames(std::vector<Hs::FinderName>& out);

// Quest NPCs: the rows of the world table hearthstone_npc_unlock (False, logged, if missing).
bool LoadHsNpcUnlockRows(std::vector<Hs::NpcUnlockRow>& out);
// The quests of questIds that any character of the account has turned in (character_queststatus rewarded = 1).
// Synchronous. False (logged) on a database failure.
bool LoadHsRewardedQuests(uint32_t accountId, std::vector<uint32_t> const& questIds, std::set<uint32_t>& out);
// The account unlocks (character table hearthstone_account_npc, migration 0006) behind Hs::NpcUnlockService.
void CheckHsNpcUnlockTable();
Hs::NpcUnlockService& HsNpcUnlocks();

// Match history (table hearthstone_match_history, character migration 0004).
// Startup check: logs a warning (never fails) when the table is missing; recording then stays off until
// the next start.
void CheckHsMatchHistoryTable();

// Queues the insert of one row ASYNCHRONOUSLY (CharacterDatabase.PExecute outside a transaction goes to the
// database thread; the caller never waits). Callable from any thread. Returns false, after logging, when
// nothing was queued (the table is missing or the queue refused it); the caller ignores it.
bool WriteHsMatchHistory(Hs::MatchHistoryRow const& row);

// The newest rows (highest id first) of an account, or of one character guid when guid != 0, at most limit.
// Synchronous (a GM command). False (logged) when the query failed.
bool LoadHsMatchHistory(uint32_t accountId, uint32_t guid, uint32_t limit, std::vector<Hs::MatchHistoryListing>& out);

// The process-wide match service.
Hs::MatchService& HsMatches();

#endif
