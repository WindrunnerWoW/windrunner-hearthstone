/*
 * Collection service: per-account cache and the mutation order of the
 * Contract (validate, commit, then update the cache), plus the pack
 * stock and the atomic pack opening. Persistence is injected
 * through CollectionStorage (database in the game, a fake in tests).
 *
 * PURE C++ (standard library only, no core headers).
 *
 * Threading: every public method takes the service mutex, and mutations keep
 * it across load, commit and cache update, so two mutations of one account can
 * never interleave. Callers must not hold a lock that the storage or a later
 * notification (COLL_CHANGED push) takes; the glue pushes after the call returns.
 */

#ifndef MOD_HEARTHSTONE_HS_COLLECTION_SERVICE_H
#define MOD_HEARTHSTONE_HS_COLLECTION_SERVICE_H

#include "HsOwnership.h"
#include "HsPacks.h"
#include "HsWallet.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Hs
{
    // One committed change of one card (card row + version bump + ledger row).
    struct Mutation
    {
        std::string cardId; // canonical dataset id
        CardCount before;
        CardCount after;
        uint32_t versionBefore = 0;
        uint32_t versionAfter = 0;
        std::string reason; // ledger reason, for example gm_grant
        std::string source; // ledger source, for example gm:5

        int32_t DeltaProtected() const
        {
            return static_cast<int32_t>(after.protectedQty) - static_cast<int32_t>(before.protectedQty);
        }
        int32_t DeltaUnprotected() const
        {
            return static_cast<int32_t>(after.unprotectedQty) - static_cast<int32_t>(before.unprotectedQty);
        }
    };

    // A GM change of the pack stock (pack row + pack ledger row).
    struct PackStockChange
    {
        PackState before;
        PackState after;
        std::string reason; // for example gm_pack_grant
        std::string source; // for example gm:5
    };

    // One pack opening: every card row with its ledger row, one collection
    // version bump, the pack row, the opening row and a pack ledger row, all
    // in one transaction.
    struct PackOpenCommit
    {
        PackState before;
        PackState after;
        PackOpening opening;
        std::vector<Mutation> cards; // one per distinct card, all with the same versions
        uint32_t versionBefore = 0;
        uint32_t versionAfter = 0;
        std::string source; // who opened it, for the pack ledger (for example char:42)
    };

    // One change of the wallet (wallet row + wallet ledger row).
    struct WalletChange
    {
        Wallet before;
        Wallet after;
        std::string reason; // for example gm_dust, craft, scrap
        std::string source; // for example char:42, gm:5
    };

    // One craft or scrap: the card row with its ledger row, one collection version bump, the wallet row with
    // its ledger row and the hearthstone_craft_op row (unique key per account), all in one transaction.
    struct CraftCommit
    {
        Mutation card;
        WalletChange wallet;
        CraftRecord record;
    };

    // Every extra copy of the collection in one go (CRAFT_EXTRAS): one card row with its ledger row per card
    // that loses copies (all with the same versions), one collection version bump, the wallet row with its ledger
    // row and the hearthstone_craft_op row, all in one transaction.
    struct CraftExtrasCommit
    {
        std::vector<Mutation> cards;
        WalletChange wallet;
        CraftRecord record;     // kind Extras, cardId kExtrasCardId, count = copies scrapped
    };

    // A commit step that replaces the storage's own transaction (see GrantVia / GrantPacksVia): it is called with
    // the planned change while the service mutex is held, and must make it durable TOGETHER with whatever else the
    // caller commits atomically with it. True only if it committed. The service never calls the storage for that
    // change; the cache follows the answer exactly like a storage commit.
    using CardCommitFn = std::function<bool(Mutation const&)>;
    using PackCommitFn = std::function<bool(PackStockChange const&)>;

    class CollectionStorage
    {
    public:
        virtual ~CollectionStorage() = default;
        // Reads the whole collection of an account (an unknown account is an
        // empty collection with version 0). False on a database failure.
        virtual bool Load(uint32_t accountId, Collection& out) = 0;
        // Writes one mutation in a single transaction. True only if it committed.
        virtual bool Commit(uint32_t accountId, Mutation const& m) = 0;

        // Pack stock and the newest openings (newest first, at most
        // kRecentOpenings) of the classic pack. Unknown account: all zero.
        virtual bool LoadPacks(uint32_t accountId, PackState& out, std::vector<PackOpening>& recent) = 0;
        // The opening stored under key, if any. False on a database failure.
        virtual bool FindPackOpening(uint32_t accountId, std::string const& key, bool& found, PackOpening& out) = 0;
        // True only if the change committed.
        virtual bool CommitPackStock(uint32_t accountId, PackStockChange const& c) = 0;
        // True only if the whole opening committed. The store rejects a key the
        // account already used (unique key), so a race cannot open twice.
        virtual bool CommitPackOpen(uint32_t accountId, PackOpenCommit const& c) = 0;
        // Several card changes with one version bump (every card row
        // with its ledger row, all mutations carry the same versions), in one
        // transaction. True only if it committed.
        virtual bool CommitCards(uint32_t accountId, std::vector<Mutation> const& cards) = 0;

        // Economy (defaults: unavailable, so older storages still compile). An unknown account has an
        // empty wallet. Each Commit* is true only if everything committed.
        virtual bool LoadWallet(uint32_t /*accountId*/, Wallet& /*out*/) { return false; }
        virtual bool CommitWallet(uint32_t /*accountId*/, WalletChange const& /*c*/) { return false; }
        // The craft or scrap stored under key, if any. False on a database failure.
        virtual bool FindCraftOp(uint32_t /*accountId*/, std::string const& /*key*/, bool& /*found*/,
                                 CraftRecord& /*out*/) { return false; }
        // The store rejects a key the account already used (unique key), so a race cannot craft twice.
        virtual bool CommitCraft(uint32_t /*accountId*/, CraftCommit const& /*c*/) { return false; }
        // Same for the extras of the whole collection (the record is stored with kind scrap and card id "all").
        virtual bool CommitCraftExtras(uint32_t /*accountId*/, CraftExtrasCommit const& /*c*/) { return false; }
    };

    enum class MutationStatus
    {
        Ok,
        Rejected,     // validation failed (see error); nothing was written
        Unavailable,  // card data not loaded or the collection could not be read
        CommitFailed  // the transaction did not commit; the cache entry was dropped
    };

    struct MutationResult
    {
        MutationStatus status = MutationStatus::Unavailable;
        OwnershipError error = OwnershipError::None;
        Mutation mutation; // filled for Ok (and for CommitFailed: what was attempted)
    };

    // Pack stock and newest openings of one account.
    struct PackView
    {
        PackState state;
        std::vector<PackOpening> recent; // newest first, at most kRecentOpenings
    };

    enum class PackStatus
    {
        Ok,
        Replay,       // PACK_OPEN with a key the account already used: the stored opening, nothing changed
        NoStock,      // no unopened pack; nothing changed
        BadKey,       // key not [0-9a-z]{12,16}
        BadCount,     // GM stock change outside 1..kMaxPacksPerOp
        Overflow,     // stock would exceed kMaxPackStock
        NotEnough,    // removing more packs than the account holds
        Refused,      // a rolled card cannot be granted (see error / card); nothing changed
        Unavailable,  // card data not loaded or the account could not be read
        CommitFailed  // the transaction did not commit; the cache entries were dropped
    };
    char const* PackStatusText(PackStatus s);

    struct PackOpenResult
    {
        PackStatus status = PackStatus::Unavailable;
        PackOpening opening;      // Ok and Replay
        uint32_t stockNow = 0;    // Ok, Replay and NoStock
        PackRoll roll;            // Ok: rarities, pity and upgrade details
        OwnershipError error = OwnershipError::None; // Refused
        std::string card;         // Refused: the card that could not be granted
    };

    // Starter grants: the protected copies a recipe needs.
    struct TopUpResult
    {
        MutationStatus status = MutationStatus::Unavailable;
        OwnershipError error = OwnershipError::None; // Rejected
        std::string card;                            // Rejected: the card that could not be granted
        std::vector<Mutation> mutations;             // Ok: what changed (empty: nothing was missing)
        uint32_t added = 0;                          // Ok: protected copies added in total
    };

    struct PackStockResult
    {
        PackStatus status = PackStatus::Unavailable;
        PackState before;
        PackState after;
    };

    // Economy.
    enum class WalletStatus
    {
        Ok,
        NotEnough,    // the dust would go below 0 (nothing changed)
        Overflow,     // the dust would exceed kMaxBalance (nothing changed)
        BadCount,     // nothing to change
        Unavailable,  // card data not loaded or the wallet could not be read
        CommitFailed  // the transaction did not commit; the cache entries were dropped
    };
    char const* WalletStatusText(WalletStatus s);

    struct WalletResult
    {
        WalletStatus status = WalletStatus::Unavailable;
        Wallet before;
        Wallet after;
    };

    enum class CraftStatus
    {
        Ok,
        Replay,       // a key the account already used: the stored outcome, nothing changed
        Refused,      // see why; nothing changed
        BadKey,       // key not [0-9a-z]{12,16}
        Unavailable,  // card data not loaded or the account could not be read
        CommitFailed  // the transaction did not commit; the cache entries were dropped
    };
    char const* CraftStatusText(CraftStatus s);

    struct CraftResult
    {
        CraftStatus status = CraftStatus::Unavailable;
        CraftWhy why = CraftWhy::None;   // Refused
        CraftRecord record;              // Ok and Replay
        Wallet wallet;                   // Ok and Replay: the wallet now; Refused: the wallet when known
        CardCount before;                // Ok: the card's counts before
        uint32_t needed = 0;             // Refused with Dust: the dust the craft costs
    };

    class CollectionService
    {
    public:
        explicit CollectionService(CollectionStorage& storage);

        // Card rules of the binary's dataset. Null (never loaded or failed):
        // every collection and pack operation answers Unavailable. Also
        // rebuilds the pack pool.
        void SetCatalog(std::shared_ptr<CardCatalog const> catalog);
        std::shared_ptr<CardCatalog const> Catalog() const;
        // Null without a catalog.
        std::shared_ptr<PackPool const> Pool() const;

        void SetPackOdds(PackOdds odds); // sanitized
        PackOdds GetPackOdds() const;
        // Replaces the random source of pack rolls (tests). The default is a
        // std::mt19937 seeded from std::random_device.
        void SetRng(PackRng rng);

        // Lifecycle. Only accounts with a character in the world keep cache
        // entries (collection and packs); SetOffline also evicts them.
        void SetOnline(uint32_t accountId);
        void SetOffline(uint32_t accountId);
        void Evict(uint32_t accountId);
        bool IsOnline(uint32_t accountId) const;

        // Current collection (cached, loaded on demand). False: Unavailable.
        bool Get(uint32_t accountId, Collection& out);

        // id is matched case-insensitively against the catalog.
        MutationResult Grant(uint32_t accountId, std::string const& id, uint32_t count, GrantSource source,
                             ProtectionMode mode, std::string const& reason, std::string const& ledgerSource);
        // Grant with a caller-owned commit (keyed grants of the arena settlement: the card rows and the caller's
        // marker row commit in one transaction). The callback must not call back into this service.
        MutationResult GrantVia(uint32_t accountId, std::string const& id, uint32_t count, GrantSource source,
                                ProtectionMode mode, std::string const& reason, std::string const& ledgerSource,
                                CardCommitFn const& commit);
        // all removes every copy (count ignored). The id must be a dataset id.
        MutationResult Remove(uint32_t accountId, std::string const& id, uint32_t count, bool all,
                              std::string const& reason, std::string const& ledgerSource);

        // Starter top-up: for every card of need (id -> copies), adds
        // protected copies until the account holds at least that many PROTECTED
        // copies; unprotected copies are kept as they are and never converted.
        // Idempotent: a second call with the same need changes nothing (Ok with
        // no mutations). All changes commit in one transaction with one version
        // bump; any card that cannot be granted (Quest source rules, the copy
        // limit) rejects the whole top-up and nothing is written.
        TopUpResult TopUpProtected(uint32_t accountId, std::map<std::string, uint32_t> const& need,
                                   std::string const& reason, std::string const& ledgerSource);

        // Pack stock and newest openings (cached like the collection). False: Unavailable.
        bool GetPacks(uint32_t accountId, PackView& out);
        // GM stock changes. all removes every pack (count ignored).
        PackStockResult GrantPacks(uint32_t accountId, uint32_t count, std::string const& reason,
                                   std::string const& ledgerSource);
        PackStockResult RemovePacks(uint32_t accountId, uint32_t count, bool all, std::string const& reason,
                                    std::string const& ledgerSource);
        // GrantPacks with a caller-owned commit (see GrantVia): the pack row and ledger row are handed to commit,
        // which writes them in the caller's transaction. The callback must not call back into this service.
        PackStockResult GrantPacksVia(uint32_t accountId, uint32_t count, std::string const& reason,
                                      std::string const& ledgerSource, PackCommitFn const& commit);
        // Opens one pack atomically: replay check by key, stock check, roll,
        // then one commit of stock, counter, opening and cards. ledgerSource
        // names the opener in the pack ledger.
        PackOpenResult OpenPack(uint32_t accountId, std::string const& key, std::string const& ledgerSource);

        // ---- economy -------------------------------------------------------------------------------
        void SetDustValues(DustValues values); // sanitized
        DustValues GetDustValues() const;

        // The dust (cached like the collection). False: Unavailable.
        bool GetWallet(uint32_t accountId, Wallet& out);
        // Adds (positive) or takes (negative) dust in one commit (GM changes, later quest rewards). 0 is BadCount; below
        // 0 is NotEnough, above kMaxBalance Overflow (nothing changed).
        WalletResult ChangeDust(uint32_t accountId, int64_t delta, std::string const& reason,
                                std::string const& ledgerSource);
        // Crafts one copy of a card (unprotected) for its dust cost. Idempotent by key.
        CraftResult Craft(uint32_t accountId, std::string const& key, std::string const& cardId,
                          std::string const& ledgerSource);
        // Scraps count unprotected copies for their dust yield (protected copies never). Idempotent by key.
        CraftResult Scrap(uint32_t accountId, std::string const& key, std::string const& cardId, uint32_t count,
                          std::string const& ledgerSource);
        // Scraps every extra copy of the collection for its dust yield: the copies above what a deck can use (2, or 1
        // for a Legendary) that are not protected, of every craftable card. Refused with CraftWhy::Nothing when there
        // are none. The record has kind Extras, card id kExtrasCardId and count = the copies. Idempotent by key.
        CraftResult ScrapExtras(uint32_t accountId, std::string const& key, std::string const& ledgerSource);
        bool IsWalletCached(uint32_t accountId) const;

        std::size_t CachedCount() const;
        bool IsCached(uint32_t accountId) const;
        bool IsPackCached(uint32_t accountId) const;

    private:
        // Require m_mutex. Cached entry, or a fresh load (cached only when online).
        bool LoadLocked(uint32_t accountId, Collection& out);
        bool LoadPacksLocked(uint32_t accountId, PackView& out);
        MutationResult CommitLocked(uint32_t accountId, Collection& current, Mutation& m,
                                    CardCommitFn const* via = nullptr);
        MutationResult GrantLocked(uint32_t accountId, std::string const& id, uint32_t count, GrantSource source,
                                   ProtectionMode mode, std::string const& reason, std::string const& ledgerSource,
                                   CardCommitFn const* via);
        PackStockResult CommitStockLocked(uint32_t accountId, PackView const& current, PackState const& after,
                                          std::string const& reason, std::string const& ledgerSource,
                                          PackCommitFn const* via = nullptr);
        PackStockResult GrantPacksLocked(uint32_t accountId, uint32_t count, std::string const& reason,
                                         std::string const& ledgerSource, PackCommitFn const* via);
        bool LoadWalletLocked(uint32_t accountId, Wallet& out);
        // Shared start of Craft and Scrap: key check, loads, replay lookup. True: go on (the loads are in the
        // out parameters); false: result is final.
        bool BeginCraftLocked(uint32_t accountId, std::string const& key, Collection& collection, Wallet& wallet,
                              CraftResult& result);
        CraftResult CommitCraftLocked(uint32_t accountId, Collection& collection, Wallet const& wallet,
                                      CraftCommit& commit);
        // Drops every cache entry of an account.
        void DropLocked(uint32_t accountId);

        CollectionStorage& m_storage;
        mutable std::mutex m_mutex;
        std::shared_ptr<CardCatalog const> m_catalog;
        std::shared_ptr<PackPool const> m_pool;
        PackOdds m_odds;
        PackRng m_rng;
        std::unordered_set<uint32_t> m_online;
        std::unordered_map<uint32_t, Collection> m_cache;
        std::unordered_map<uint32_t, PackView> m_packs;
        DustValues m_dust;
        std::unordered_map<uint32_t, Wallet> m_wallets;
    };
}

#endif
