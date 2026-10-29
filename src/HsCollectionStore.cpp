#include "HsCollectionStore.h"

#include "Database/DatabaseEnv.h"
#include "Log.h"

#include "HsDataset.h"

#include <algorithm>
#include <string>
#include <utility>

namespace
{
    std::string Escaped(std::string s, std::size_t maxLen)
    {
        if (s.size() > maxLen)
            s.resize(maxLen);
        CharacterDatabase.escape_string(s);
        return s;
    }

    // Queues the card row and its ledger row of one mutation (inside a transaction).
    bool QueueCardChange(uint32_t accountId, Hs::Mutation const& m)
    {
        std::string const cardId = Escaped(m.cardId, Hs::kMaxCardIdLen);
        std::string const reason = Escaped(m.reason, 32);
        std::string const source = Escaped(m.source, 64);

        // Execute() only queues inside a transaction; false means the statement was
        // not queued (for example truncated), so the transaction must not commit.
        bool queued;
        if (m.after.Total() == 0)
            queued = CharacterDatabase.PExecute(
                "DELETE FROM `hearthstone_account_card` WHERE `account_id` = %u AND `card_id` = '%s'",
                accountId, cardId.c_str());
        else
            queued = CharacterDatabase.PExecute(
                "INSERT INTO `hearthstone_account_card` (`account_id`, `card_id`, `qty_protected`, `qty_unprotected`) "
                "VALUES (%u, '%s', %u, %u) ON DUPLICATE KEY UPDATE `qty_protected` = %u, `qty_unprotected` = %u",
                accountId, cardId.c_str(), m.after.protectedQty, m.after.unprotectedQty,
                m.after.protectedQty, m.after.unprotectedQty);

        return queued && CharacterDatabase.PExecute(
            "INSERT INTO `hearthstone_card_ledger` (`account_id`, `card_id`, `delta_protected`, `delta_unprotected`, "
            "`version_after`, `reason`, `source`) VALUES (%u, '%s', %d, %d, %u, '%s', '%s')",
            accountId, cardId.c_str(), static_cast<int>(m.DeltaProtected()), static_cast<int>(m.DeltaUnprotected()),
            m.versionAfter, reason.c_str(), source.c_str());
    }

    bool QueueCollectionVersion(uint32_t accountId, uint32_t version)
    {
        return CharacterDatabase.PExecute(
            "INSERT INTO `hearthstone_account` (`account_id`, `collection_version`) VALUES (%u, %u) "
            "ON DUPLICATE KEY UPDATE `collection_version` = %u",
            accountId, version, version);
    }

    // Queues the pack row and a pack ledger row (inside a transaction).
    bool QueuePackChange(uint32_t accountId, Hs::PackState const& before, Hs::PackState const& after,
                         std::string const& reasonRaw, std::string const& sourceRaw)
    {
        std::string const reason = Escaped(reasonRaw, 32);
        std::string const source = Escaped(sourceRaw, 64);
        int64 const delta = static_cast<int64>(after.stock) - static_cast<int64>(before.stock);
        return CharacterDatabase.PExecute(
            "INSERT INTO `hearthstone_account_pack` (`account_id`, `pack_type`, `stock`, `opened`, `since_legendary`) "
            "VALUES (%u, '%s', %u, %u, %u) ON DUPLICATE KEY UPDATE `stock` = %u, `opened` = %u, `since_legendary` = %u",
            accountId, Hs::kPackTypeClassic, after.stock, after.opened, after.sinceLegendary,
            after.stock, after.opened, after.sinceLegendary) &&
            CharacterDatabase.PExecute(
            "INSERT INTO `hearthstone_pack_ledger` (`account_id`, `pack_type`, `delta`, `stock_after`, `reason`, `source`) "
            "VALUES (%u, '%s', %d, %u, '%s', '%s')",
            accountId, Hs::kPackTypeClassic, static_cast<int>(delta), after.stock, reason.c_str(), source.c_str());
    }

    // Queues the wallet row and a wallet ledger row (inside a transaction).
    bool QueueWalletChange(uint32_t accountId, Hs::WalletChange const& c)
    {
        std::string const reason = Escaped(c.reason, 32);
        std::string const source = Escaped(c.source, 64);
        int64 const dustDelta = static_cast<int64>(c.after.dust) - static_cast<int64>(c.before.dust);
        return CharacterDatabase.PExecute(
            "INSERT INTO `hearthstone_account_wallet` (`account_id`, `dust`) VALUES (%u, %u) "
            "ON DUPLICATE KEY UPDATE `dust` = %u",
            accountId, c.after.dust, c.after.dust) &&
            CharacterDatabase.PExecute(
            "INSERT INTO `hearthstone_wallet_ledger` (`account_id`, `dust_delta`, `dust_after`, `reason`, `source`) "
            "VALUES (%u, %d, %u, '%s', '%s')",
            accountId, static_cast<int>(dustDelta), c.after.dust, reason.c_str(), source.c_str());
    }

    // Columns: seq, op_key, cards, new_mask, collection_version, stock_after,
    // since_legendary_before, since_legendary_after, pity_applied
    char const* const kOpeningColumns =
        "`seq`, `op_key`, `cards`, `new_mask`, `collection_version`, `stock_after`, "
        "`since_legendary_before`, `since_legendary_after`, `pity_applied`";

    bool ReadOpening(Field* f, Hs::PackOpening& out)
    {
        Hs::PackOpening o;
        o.seq = f[0].GetUInt32();
        o.key = f[1].GetCppString();
        if (!o.SetCardList(f[2].GetCppString(), ',') || !o.SetNewMask(f[3].GetCppString()))
            return false;
        o.collectionVersion = f[4].GetUInt32();
        o.stockAfter = f[5].GetUInt32();
        o.sinceLegendaryBefore = f[6].GetUInt32();
        o.sinceLegendaryAfter = f[7].GetUInt32();
        o.pityApplied = f[8].GetUInt32() != 0;
        out = o;
        return true;
    }

    // Runs the queued statements synchronously inside BEGIN/COMMIT (rolls back
    // on the first failing statement), or rolls back if something was not queued.
    bool FinishTransaction(bool queued, char const* what, uint32_t accountId)
    {
        if (!queued)
        {
            CharacterDatabase.RollbackTransaction();
            sLog.outError("Hearthstone: could not queue the %s of account %u", what, accountId);
            return false;
        }
        if (!CharacterDatabase.CommitTransactionDirect())
        {
            sLog.outError("Hearthstone: commit failed for the %s of account %u; its outcome is unknown, the account is read from the database again", what, accountId);
            return false;
        }
        return true;
    }
}

bool HsCollectionDbStorage::Load(uint32_t accountId, Hs::Collection& out)
{
    // A plain SELECT returns null both for "no rows" and for a failed query. This
    // one (two scalar subqueries, no FROM) always yields exactly one row, so null
    // here means a failure, and the card count tells whether a null from the
    // second query is one too.
    std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
        "SELECT (SELECT COALESCE(MAX(`collection_version`), 0) FROM `hearthstone_account` WHERE `account_id` = %u), "
        "(SELECT COUNT(*) FROM `hearthstone_account_card` WHERE `account_id` = %u)", accountId, accountId));
    if (!head)
    {
        sLog.outError("Hearthstone: cannot read the collection of account %u "
            "(character migration 0001_hearthstone_collection.sql not applied, or database error)", accountId);
        return false;
    }

    Hs::Collection collection;
    Field* fields = head->Fetch();
    collection.version = fields[0].GetUInt32();
    uint64 const rows = fields[1].GetUInt64();

    if (rows > 0)
    {
        std::unique_ptr<QueryResult> cards(CharacterDatabase.PQuery(
            "SELECT `card_id`, `qty_protected`, `qty_unprotected` FROM `hearthstone_account_card` "
            "WHERE `account_id` = %u", accountId));
        if (!cards)
        {
            sLog.outError("Hearthstone: cannot read the card rows of account %u", accountId);
            return false;
        }
        do
        {
            Field* row = cards->Fetch();
            Hs::CardCount count;
            count.protectedQty = row[1].GetUInt32();
            count.unprotectedQty = row[2].GetUInt32();
            collection.Set(row[0].GetCppString(), count); // 0/0 rows are skipped
        } while (cards->NextRow());
    }

    out = std::move(collection);
    return true;
}

bool HsCollectionDbStorage::Commit(uint32_t accountId, Hs::Mutation const& m)
{
    // The card id is a validated dataset id and reason/source are internal, but
    // everything textual is escaped anyway (QueueCardChange).
    if (!CharacterDatabase.BeginTransaction())
    {
        sLog.outError("Hearthstone: BeginTransaction failed for account %u card %s", accountId, m.cardId.c_str());
        return false;
    }
    bool const queued = QueueCardChange(accountId, m) && QueueCollectionVersion(accountId, m.versionAfter);
    if (!FinishTransaction(queued, "collection change", accountId))
    {
        sLog.outError("Hearthstone: the change of card %s (version %u) of account %u was not written",
            m.cardId.c_str(), m.versionAfter, accountId);
        return false;
    }
    return true;
}

bool HsCollectionDbStorage::CommitCards(uint32_t accountId, std::vector<Hs::Mutation> const& cards)
{
    if (cards.empty())
        return true;
    if (!CharacterDatabase.BeginTransaction())
    {
        sLog.outError("Hearthstone: BeginTransaction failed for %u card changes of account %u",
            static_cast<uint32>(cards.size()), accountId);
        return false;
    }
    bool queued = true;
    for (Hs::Mutation const& m : cards)
        queued = queued && QueueCardChange(accountId, m);
    queued = queued && QueueCollectionVersion(accountId, cards.front().versionAfter);
    if (!FinishTransaction(queued, "card grant", accountId))
    {
        sLog.outError("Hearthstone: %u card changes (version %u) of account %u were not written",
            static_cast<uint32>(cards.size()), cards.front().versionAfter, accountId);
        return false;
    }
    return true;
}

bool HsCollectionDbStorage::LoadPacks(uint32_t accountId, Hs::PackState& out, std::vector<Hs::PackOpening>& recent)
{
    // Same one-row trick as Load(): null means a failed query.
    std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
        "SELECT (SELECT COUNT(*) FROM `hearthstone_account_pack` WHERE `account_id` = %u AND `pack_type` = '%s'), "
        "(SELECT COUNT(*) FROM `hearthstone_pack_opening` WHERE `account_id` = %u AND `pack_type` = '%s')",
        accountId, Hs::kPackTypeClassic, accountId, Hs::kPackTypeClassic));
    if (!head)
    {
        sLog.outError("Hearthstone: cannot read the packs of account %u "
            "(character migration 0002_hearthstone_packs.sql not applied, or database error)", accountId);
        return false;
    }
    Field* counts = head->Fetch();
    uint64 const packRows = counts[0].GetUInt64();
    uint64 const openingRows = counts[1].GetUInt64();

    Hs::PackState state;
    if (packRows > 0)
    {
        std::unique_ptr<QueryResult> row(CharacterDatabase.PQuery(
            "SELECT `stock`, `opened`, `since_legendary` FROM `hearthstone_account_pack` "
            "WHERE `account_id` = %u AND `pack_type` = '%s'", accountId, Hs::kPackTypeClassic));
        if (!row)
        {
            sLog.outError("Hearthstone: cannot read the pack row of account %u", accountId);
            return false;
        }
        Field* f = row->Fetch();
        state.stock = f[0].GetUInt32();
        state.opened = f[1].GetUInt32();
        state.sinceLegendary = f[2].GetUInt32();
    }

    std::vector<Hs::PackOpening> openings;
    if (openingRows > 0)
    {
        std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery(
            "SELECT %s FROM `hearthstone_pack_opening` WHERE `account_id` = %u AND `pack_type` = '%s' "
            "ORDER BY `seq` DESC LIMIT %u", kOpeningColumns, accountId, Hs::kPackTypeClassic,
            static_cast<uint32>(Hs::kRecentOpenings)));
        if (!rows)
        {
            sLog.outError("Hearthstone: cannot read the pack openings of account %u", accountId);
            return false;
        }
        do
        {
            Hs::PackOpening o;
            if (ReadOpening(rows->Fetch(), o))
                openings.push_back(o);
            else
                sLog.outError("Hearthstone: skipped a malformed hearthstone_pack_opening row of account %u", accountId);
        } while (rows->NextRow());
    }

    out = state;
    recent = std::move(openings);
    return true;
}

bool HsCollectionDbStorage::FindPackOpening(uint32_t accountId, std::string const& keyRaw, bool& found,
                                            Hs::PackOpening& out)
{
    std::string const key = Escaped(keyRaw, Hs::kMaxPackKeyLen);
    std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
        "SELECT (SELECT COUNT(*) FROM `hearthstone_pack_opening` WHERE `account_id` = %u AND `op_key` = '%s')",
        accountId, key.c_str()));
    if (!head)
    {
        sLog.outError("Hearthstone: cannot look up pack key %s of account %u", key.c_str(), accountId);
        return false;
    }
    found = false;
    if (head->Fetch()[0].GetUInt64() == 0)
        return true;

    std::unique_ptr<QueryResult> row(CharacterDatabase.PQuery(
        "SELECT %s FROM `hearthstone_pack_opening` WHERE `account_id` = %u AND `op_key` = '%s'",
        kOpeningColumns, accountId, key.c_str()));
    if (!row || !ReadOpening(row->Fetch(), out))
    {
        sLog.outError("Hearthstone: cannot read the opening of pack key %s of account %u", key.c_str(), accountId);
        return false;
    }
    found = true;
    return true;
}

bool HsCollectionDbStorage::CommitPackStock(uint32_t accountId, Hs::PackStockChange const& c)
{
    if (!CharacterDatabase.BeginTransaction())
    {
        sLog.outError("Hearthstone: BeginTransaction failed for the packs of account %u", accountId);
        return false;
    }
    bool const queued = QueuePackChange(accountId, c.before, c.after, c.reason, c.source);
    return FinishTransaction(queued, "pack stock change", accountId);
}

bool HsCollectionDbStorage::CommitPackOpen(uint32_t accountId, Hs::PackOpenCommit const& c)
{
    Hs::PackOpening const& o = c.opening;
    std::string const key = Escaped(o.key, Hs::kMaxPackKeyLen);
    std::string const cards = Escaped(o.CardList(','), 255);

    if (!CharacterDatabase.BeginTransaction())
    {
        sLog.outError("Hearthstone: BeginTransaction failed for a pack opening of account %u", accountId);
        return false;
    }

    bool queued = true;
    for (Hs::Mutation const& m : c.cards)
        queued = queued && QueueCardChange(accountId, m);
    queued = queued && QueueCollectionVersion(accountId, c.versionAfter);
    queued = queued && QueuePackChange(accountId, c.before, c.after, "pack_open", c.source);
    // The unique key (account_id, op_key) fails this statement, and so the whole
    // transaction, if the key was already used.
    queued = queued && CharacterDatabase.PExecute(
        "INSERT INTO `hearthstone_pack_opening` (`account_id`, `pack_type`, `seq`, `op_key`, `cards`, `new_mask`, "
        "`collection_version`, `stock_after`, `since_legendary_before`, `since_legendary_after`, `pity_applied`) "
        "VALUES (%u, '%s', %u, '%s', '%s', '%s', %u, %u, %u, %u, %u)",
        accountId, Hs::kPackTypeClassic, o.seq, key.c_str(), cards.c_str(), o.NewMask().c_str(),
        o.collectionVersion, o.stockAfter, o.sinceLegendaryBefore, o.sinceLegendaryAfter, o.pityApplied ? 1u : 0u);

    if (!FinishTransaction(queued, "pack opening", accountId))
    {
        sLog.outError("Hearthstone: pack opening #%u (key %s) of account %u was not written", o.seq, key.c_str(),
            accountId);
        return false;
    }
    return true;
}

bool HsCollectionDbStorage::LoadWallet(uint32_t accountId, Hs::Wallet& out)
{
    // Same one-row trick as Load(): null means a failed query (or the migration is missing).
    std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
        "SELECT (SELECT COUNT(*) FROM `hearthstone_account_wallet` WHERE `account_id` = %u)", accountId));
    if (!head)
    {
        sLog.outError("Hearthstone: cannot read the wallet of account %u "
            "(character migration 0007_hearthstone_wallet.sql not applied, or database error)", accountId);
        return false;
    }
    Hs::Wallet wallet;
    if (head->Fetch()[0].GetUInt64() > 0)
    {
        std::unique_ptr<QueryResult> row(CharacterDatabase.PQuery(
            "SELECT `dust` FROM `hearthstone_account_wallet` WHERE `account_id` = %u", accountId));
        if (!row)
        {
            sLog.outError("Hearthstone: cannot read the wallet row of account %u", accountId);
            return false;
        }
        wallet.dust = row->Fetch()[0].GetUInt32();
    }
    out = wallet;
    return true;
}

bool HsCollectionDbStorage::CommitWallet(uint32_t accountId, Hs::WalletChange const& c)
{
    if (!CharacterDatabase.BeginTransaction())
    {
        sLog.outError("Hearthstone: BeginTransaction failed for the wallet of account %u", accountId);
        return false;
    }
    bool const queued = QueueWalletChange(accountId, c);
    if (!FinishTransaction(queued, "wallet change", accountId))
    {
        sLog.outError("Hearthstone: the wallet change %s (dust %u -> %u) of account %u was not written",
            c.reason.c_str(), c.before.dust, c.after.dust, accountId);
        return false;
    }
    return true;
}

bool HsCollectionDbStorage::FindCraftOp(uint32_t accountId, std::string const& keyRaw, bool& found,
                                        Hs::CraftRecord& out)
{
    std::string const key = Escaped(keyRaw, Hs::kMaxCraftKeyLen);
    std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
        "SELECT (SELECT COUNT(*) FROM `hearthstone_craft_op` WHERE `account_id` = %u AND `op_key` = '%s')",
        accountId, key.c_str()));
    if (!head)
    {
        sLog.outError("Hearthstone: cannot look up craft key %s of account %u "
            "(character migration 0007_hearthstone_wallet.sql not applied, or database error)", key.c_str(), accountId);
        return false;
    }
    found = false;
    if (head->Fetch()[0].GetUInt64() == 0)
        return true;

    std::unique_ptr<QueryResult> row(CharacterDatabase.PQuery(
        "SELECT `kind`, `card_id`, `count`, `dust_delta`, `dust_after`, `collection_version`, `card_total`, "
        "`card_protected` FROM `hearthstone_craft_op` WHERE `account_id` = %u AND `op_key` = '%s'",
        accountId, key.c_str()));
    if (!row)
    {
        sLog.outError("Hearthstone: cannot read the craft of key %s of account %u", key.c_str(), accountId);
        return false;
    }
    Field* f = row->Fetch();
    Hs::CraftRecord r;
    r.key = keyRaw;
    r.cardId = f[1].GetCppString();
    // an extras op is stored as a scrap of the card id "all" (the kind column is an ENUM of craft and scrap)
    if (f[0].GetCppString() != "scrap")
        r.kind = Hs::CraftKind::Make;
    else
        r.kind = r.cardId == Hs::kExtrasCardId ? Hs::CraftKind::Extras : Hs::CraftKind::Scrap;
    r.count = f[2].GetUInt32();
    r.dustDelta = f[3].GetInt32();
    r.dustAfter = f[4].GetUInt32();
    r.collectionVersion = f[5].GetUInt32();
    uint32 const total = f[6].GetUInt32();
    r.cardAfter.protectedQty = std::min<uint32>(f[7].GetUInt32(), total);
    r.cardAfter.unprotectedQty = total - r.cardAfter.protectedQty;
    out = r;
    found = true;
    return true;
}

bool HsCollectionDbStorage::CommitCraft(uint32_t accountId, Hs::CraftCommit const& c)
{
    Hs::CraftRecord const& r = c.record;
    std::string const key = Escaped(r.key, Hs::kMaxCraftKeyLen);
    std::string const cardId = Escaped(r.cardId, Hs::kMaxCardIdLen);
    if (!CharacterDatabase.BeginTransaction())
    {
        sLog.outError("Hearthstone: BeginTransaction failed for a %s of account %u", Hs::CraftKindCode(r.kind), accountId);
        return false;
    }
    bool queued = QueueCardChange(accountId, c.card) && QueueCollectionVersion(accountId, c.card.versionAfter) &&
        QueueWalletChange(accountId, c.wallet);
    // The unique key (account_id, op_key) fails this statement, and so the whole transaction, if the key was
    // already used.
    queued = queued && CharacterDatabase.PExecute(
        "INSERT INTO `hearthstone_craft_op` (`account_id`, `op_key`, `kind`, `card_id`, `count`, `dust_delta`, "
        "`dust_after`, `collection_version`, `card_total`, `card_protected`) "
        "VALUES (%u, '%s', '%s', '%s', %u, %d, %u, %u, %u, %u)",
        accountId, key.c_str(), Hs::CraftKindCode(r.kind), cardId.c_str(), r.count, static_cast<int>(r.dustDelta),
        r.dustAfter, r.collectionVersion, r.cardAfter.Total(), r.cardAfter.protectedQty);
    if (!FinishTransaction(queued, Hs::CraftKindCode(r.kind), accountId))
    {
        sLog.outError("Hearthstone: %s of %s (key %s) of account %u was not written", Hs::CraftKindCode(r.kind),
            cardId.c_str(), key.c_str(), accountId);
        return false;
    }
    return true;
}

bool HsCollectionDbStorage::CommitCraftExtras(uint32_t accountId, Hs::CraftExtrasCommit const& c)
{
    Hs::CraftRecord const& r = c.record;
    std::string const key = Escaped(r.key, Hs::kMaxCraftKeyLen);
    if (c.cards.empty() || !CharacterDatabase.BeginTransaction())
    {
        sLog.outError("Hearthstone: BeginTransaction failed for the extras scrap of account %u", accountId);
        return false;
    }
    bool queued = true;
    for (Hs::Mutation const& m : c.cards)
        queued = queued && QueueCardChange(accountId, m);
    queued = queued && QueueCollectionVersion(accountId, c.cards.front().versionAfter) &&
        QueueWalletChange(accountId, c.wallet);
    // The unique key (account_id, op_key) fails this statement, and so the whole transaction, if the key was
    // already used. The kind column is an ENUM of craft and scrap: an extras op is a scrap of the card id "all".
    queued = queued && CharacterDatabase.PExecute(
        "INSERT INTO `hearthstone_craft_op` (`account_id`, `op_key`, `kind`, `card_id`, `count`, `dust_delta`, "
        "`dust_after`, `collection_version`, `card_total`, `card_protected`) "
        "VALUES (%u, '%s', 'scrap', '%s', %u, %d, %u, %u, 0, 0)",
        accountId, key.c_str(), Hs::kExtrasCardId, r.count, static_cast<int>(r.dustDelta), r.dustAfter,
        r.collectionVersion);
    if (!FinishTransaction(queued, "extras scrap", accountId))
    {
        sLog.outError("Hearthstone: the extras scrap (%u cards, key %s) of account %u was not written",
            static_cast<uint32>(c.cards.size()), key.c_str(), accountId);
        return false;
    }
    return true;
}

bool HsQueuePackChange(uint32_t accountId, Hs::PackStockChange const& change)
{
    return QueuePackChange(accountId, change.before, change.after, change.reason, change.source);
}

bool HsQueueCardMutation(uint32_t accountId, Hs::Mutation const& mutation)
{
    return QueueCardChange(accountId, mutation) && QueueCollectionVersion(accountId, mutation.versionAfter);
}

std::shared_ptr<Hs::CardCatalog const> LoadHsCardCatalog()
{
    std::unique_ptr<QueryResult> rows(WorldDatabase.PQuery(
        "SELECT `id`, `collectible`, `acquisition`, `rarity`, `card_class` FROM `hearthstone_card_template` "
        "WHERE `dataset_id` = '%s'", Hs::kDatasetId));
    if (!rows)
    {
        sLog.outError("Hearthstone: no hearthstone_card_template rows for dataset %s in the world database; "
            "card collections are UNAVAILABLE until the world migration is applied and the server restarted",
            Hs::kDatasetId);
        return nullptr;
    }

    auto catalog = std::make_shared<Hs::CardCatalog>();
    uint32 skipped = 0;
    do
    {
        Field* f = rows->Fetch();
        std::string const rarity = f[3].IsNULL() ? std::string() : f[3].GetCppString();
        if (!catalog->Add(f[0].GetCppString(), f[1].GetUInt32() != 0, f[2].GetCppString(), rarity, f[4].GetCppString()))
            ++skipped;
    } while (rows->NextRow());

    if (skipped)
        sLog.outError("Hearthstone: skipped %u invalid hearthstone_card_template rows of dataset %s", skipped, Hs::kDatasetId);
    if (catalog->Size() != Hs::kDatasetCardCount)
        sLog.outError("Hearthstone: WARNING loaded %u card rules for dataset %s, the binary expects %u",
            static_cast<uint32>(catalog->Size()), Hs::kDatasetId, Hs::kDatasetCardCount);
    else
        sLog.outString("Hearthstone: loaded %u card rules for collections", static_cast<uint32>(catalog->Size()));
    return catalog;
}

void CheckHsCollectionTables()
{
    struct Table { char const* name; char const* migration; };
    Table const tables[] = {
        { "hearthstone_account_card", "0001_hearthstone_collection.sql" },
        { "hearthstone_account", "0001_hearthstone_collection.sql" },
        { "hearthstone_card_ledger", "0001_hearthstone_collection.sql" },
        { "hearthstone_account_pack", "0002_hearthstone_packs.sql" },
        { "hearthstone_pack_opening", "0002_hearthstone_packs.sql" },
        { "hearthstone_pack_ledger", "0002_hearthstone_packs.sql" },
        { "hearthstone_account_wallet", "0007_hearthstone_wallet.sql" },
        { "hearthstone_wallet_ledger", "0007_hearthstone_wallet.sql" },
        { "hearthstone_craft_op", "0007_hearthstone_wallet.sql" },
        { "hearthstone_account_intro", "0008_hearthstone_intro.sql" },
    };
    for (Table const& table : tables)
    {
        std::unique_ptr<QueryResult> found(CharacterDatabase.PQuery("SHOW TABLES LIKE '%s'", table.name));
        if (!found)
            sLog.outError("Hearthstone: table %s is missing in the character database "
                "(migration %s not applied? check Database.AutoUpdate.*)", table.name, table.migration);
    }
}

Hs::CollectionService& HsCollections()
{
    static HsCollectionDbStorage storage;
    static Hs::CollectionService service(storage);
    return service;
}
