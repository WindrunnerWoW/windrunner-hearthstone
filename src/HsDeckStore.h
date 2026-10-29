/*
 * mod-hearthstone: database glue of the decks and the account class unlocks.
 * The rules and the cache live in src/decks (pure C++); this file only reads
 * and writes rows.
 *
 * Tables (CHARACTER database, keyed by account id), migration
 * 0003_hearthstone_decks.sql: hearthstone_deck, hearthstone_deck_op,
 * hearthstone_deck_ledger, hearthstone_account_class, hearthstone_class_ledger;
 * migration 0005_hearthstone_starters.sql: hearthstone_account_starter.
 */

#ifndef MOD_HEARTHSTONE_HS_DECK_STORE_H
#define MOD_HEARTHSTONE_HS_DECK_STORE_H

#include "decks/HsDeckService.h"

#include <string>

// Synchronous CharacterDatabase implementation of Hs::DeckStorage.
class HsDeckDbStorage : public Hs::DeckStorage
{
public:
    bool LoadBook(uint32_t accountId, Hs::DeckBook& out) override;
    bool FindDeckOp(uint32_t accountId, std::string const& key, bool& found, Hs::DeckOp& out) override;
    bool CommitDeck(uint32_t accountId, Hs::DeckCommit const& c) override;
    bool CommitClass(uint32_t accountId, Hs::ClassCommit const& c) override;
    bool CommitStarter(uint32_t accountId, Hs::StarterCommit const& c) override;
};

// Logs an error if a character table of the decks is missing.
void CheckHsDeckTables();

// The process-wide deck service (backed by HsDeckDbStorage and HsCollections()).
Hs::DeckService& HsDecks();

#endif
