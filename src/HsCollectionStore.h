/*
 * mod-hearthstone: database glue of the account collection and packs. The
 * rules and the cache live in src/collection (pure C++); this file only reads
 * and writes rows.
 *
 * Tables (CHARACTER database, keyed by account id):
 *   0001_hearthstone_collection.sql: hearthstone_account_card,
 *     hearthstone_account, hearthstone_card_ledger
 *   0002_hearthstone_packs.sql: hearthstone_account_pack,
 *     hearthstone_pack_opening, hearthstone_pack_ledger
 *   0007_hearthstone_wallet.sql (economy): hearthstone_account_wallet,
 *     hearthstone_wallet_ledger, hearthstone_craft_op
 * Card rules come from the world table hearthstone_card_template (data/sql/world).
 */

#ifndef MOD_HEARTHSTONE_HS_COLLECTION_STORE_H
#define MOD_HEARTHSTONE_HS_COLLECTION_STORE_H

#include "collection/HsCollectionService.h"

#include <memory>
#include <string>
#include <vector>

// Synchronous CharacterDatabase implementation of Hs::CollectionStorage.
class HsCollectionDbStorage : public Hs::CollectionStorage
{
public:
    bool Load(uint32_t accountId, Hs::Collection& out) override;
    bool Commit(uint32_t accountId, Hs::Mutation const& m) override;
    bool LoadPacks(uint32_t accountId, Hs::PackState& out, std::vector<Hs::PackOpening>& recent) override;
    bool FindPackOpening(uint32_t accountId, std::string const& key, bool& found, Hs::PackOpening& out) override;
    bool CommitPackStock(uint32_t accountId, Hs::PackStockChange const& c) override;
    bool CommitPackOpen(uint32_t accountId, Hs::PackOpenCommit const& c) override;
    bool CommitCards(uint32_t accountId, std::vector<Hs::Mutation> const& cards) override;
    // Economy (0007_hearthstone_wallet.sql).
    bool LoadWallet(uint32_t accountId, Hs::Wallet& out) override;
    bool CommitWallet(uint32_t accountId, Hs::WalletChange const& c) override;
    bool FindCraftOp(uint32_t accountId, std::string const& key, bool& found, Hs::CraftRecord& out) override;
    bool CommitCraft(uint32_t accountId, Hs::CraftCommit const& c) override;
    bool CommitCraftExtras(uint32_t accountId, Hs::CraftExtrasCommit const& c) override;
};

// Queue the rows of one planned change into the character database transaction that is open on this thread (the
// caller began it and commits it): for keyed grants that must commit together with the caller's own rows (the arena
// settlement, HsArenaSettle.cpp). False if a statement could not be queued (the caller rolls back).
bool HsQueuePackChange(uint32_t accountId, Hs::PackStockChange const& change);
bool HsQueueCardMutation(uint32_t accountId, Hs::Mutation const& mutation);

// Card rules of Hs::kDatasetId from the world database. Null (logged) if the
// table is missing or has no rows for the dataset.
std::shared_ptr<Hs::CardCatalog const> LoadHsCardCatalog();

// Logs an error if a character table of the collection or the packs is missing.
void CheckHsCollectionTables();

// The process-wide collection service (backed by HsCollectionDbStorage).
Hs::CollectionService& HsCollections();

#endif
