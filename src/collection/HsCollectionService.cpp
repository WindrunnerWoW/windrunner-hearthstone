#include "HsCollectionService.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <random>
#include <utility>

namespace Hs
{
    char const* PackStatusText(PackStatus s)
    {
        switch (s)
        {
            case PackStatus::Ok: return "ok";
            case PackStatus::Replay: return "already opened with this key";
            case PackStatus::NoStock: return "no unopened pack";
            case PackStatus::BadKey: return "bad pack key";
            case PackStatus::BadCount: return "count must be 1..100";
            case PackStatus::Overflow: return "an account may hold at most 100000 unopened packs";
            case PackStatus::NotEnough: return "not enough unopened packs";
            case PackStatus::Refused: return "a card of the pack cannot be granted";
            case PackStatus::Unavailable: return "packs unavailable";
            case PackStatus::CommitFailed: return "commit failed";
        }
        return "error";
    }

    CollectionService::CollectionService(CollectionStorage& storage) : m_storage(storage)
    {
        auto engine = std::make_shared<std::mt19937>();
        std::random_device device;
        std::seed_seq seed { device(), device(),
            static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()) };
        engine->seed(seed);
        m_rng = [engine](uint32_t bound)
        {
            return std::uniform_int_distribution<uint32_t>(0, bound - 1)(*engine);
        };
    }

    void CollectionService::SetCatalog(std::shared_ptr<CardCatalog const> catalog)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_catalog = std::move(catalog);
        m_pool = m_catalog ? std::make_shared<PackPool const>(PackPool::FromCatalog(*m_catalog)) : nullptr;
    }

    std::shared_ptr<CardCatalog const> CollectionService::Catalog() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_catalog;
    }

    std::shared_ptr<PackPool const> CollectionService::Pool() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_pool;
    }

    void CollectionService::SetPackOdds(PackOdds odds)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_odds = SanitizePackOdds(odds);
    }

    PackOdds CollectionService::GetPackOdds() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_odds;
    }

    void CollectionService::SetRng(PackRng rng)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_rng = std::move(rng);
    }

    void CollectionService::DropLocked(uint32_t accountId)
    {
        m_cache.erase(accountId);
        m_packs.erase(accountId);
        m_wallets.erase(accountId);
    }

    void CollectionService::SetOnline(uint32_t accountId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_online.insert(accountId);
        DropLocked(accountId); // a fresh login reads the database again
    }

    void CollectionService::SetOffline(uint32_t accountId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_online.erase(accountId);
        DropLocked(accountId);
    }

    void CollectionService::Evict(uint32_t accountId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        DropLocked(accountId);
    }

    bool CollectionService::IsOnline(uint32_t accountId) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_online.count(accountId) != 0;
    }

    std::size_t CollectionService::CachedCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_cache.size();
    }

    bool CollectionService::IsCached(uint32_t accountId) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_cache.count(accountId) != 0;
    }

    bool CollectionService::IsPackCached(uint32_t accountId) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_packs.count(accountId) != 0;
    }

    bool CollectionService::LoadPacksLocked(uint32_t accountId, PackView& out)
    {
        auto const it = m_packs.find(accountId);
        if (it != m_packs.end())
        {
            out = it->second;
            return true;
        }
        PackView loaded;
        if (!m_storage.LoadPacks(accountId, loaded.state, loaded.recent))
            return false;
        if (loaded.recent.size() > kRecentOpenings)
            loaded.recent.resize(kRecentOpenings);
        if (m_online.count(accountId))
            m_packs[accountId] = loaded;
        out = std::move(loaded);
        return true;
    }

    bool CollectionService::LoadLocked(uint32_t accountId, Collection& out)
    {
        auto const it = m_cache.find(accountId);
        if (it != m_cache.end())
        {
            out = it->second;
            return true;
        }
        Collection loaded;
        if (!m_storage.Load(accountId, loaded))
            return false;
        if (m_online.count(accountId))
            m_cache[accountId] = loaded;
        out = std::move(loaded);
        return true;
    }

    bool CollectionService::Get(uint32_t accountId, Collection& out)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_catalog)
            return false;
        return LoadLocked(accountId, out);
    }

    MutationResult CollectionService::CommitLocked(uint32_t accountId, Collection& current, Mutation& m,
                                                   CardCommitFn const* via)
    {
        MutationResult result;
        m.versionBefore = current.version;
        m.versionAfter = current.version + 1;
        result.mutation = m;

        if (!(via ? (*via)(m) : m_storage.Commit(accountId, m)))
        {
            // The database state is unknown now: read it again next time.
            m_cache.erase(accountId);
            result.status = MutationStatus::CommitFailed;
            return result;
        }

        current.version = m.versionAfter;
        current.Set(m.cardId, m.after);
        if (m_online.count(accountId))
            m_cache[accountId] = current;
        else
            m_cache.erase(accountId);
        result.status = MutationStatus::Ok;
        return result;
    }

    MutationResult CollectionService::Grant(uint32_t accountId, std::string const& id, uint32_t count,
                                            GrantSource source, ProtectionMode mode, std::string const& reason,
                                            std::string const& ledgerSource)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return GrantLocked(accountId, id, count, source, mode, reason, ledgerSource, nullptr);
    }

    MutationResult CollectionService::GrantVia(uint32_t accountId, std::string const& id, uint32_t count,
                                               GrantSource source, ProtectionMode mode, std::string const& reason,
                                               std::string const& ledgerSource, CardCommitFn const& commit)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return GrantLocked(accountId, id, count, source, mode, reason, ledgerSource, &commit);
    }

    MutationResult CollectionService::GrantLocked(uint32_t accountId, std::string const& id, uint32_t count,
                                                  GrantSource source, ProtectionMode mode, std::string const& reason,
                                                  std::string const& ledgerSource, CardCommitFn const* via)
    {
        MutationResult result;
        if (!m_catalog)
            return result; // Unavailable

        Mutation m;
        CardRule rule;
        result.error = ValidateGrant(*m_catalog, id, source, m.cardId, rule);
        if (result.error != OwnershipError::None)
        {
            result.status = MutationStatus::Rejected;
            return result;
        }
        // Count bounds need no database access: reject them before loading.
        if (count < 1 || count > kMaxCountPerOp)
        {
            result.status = MutationStatus::Rejected;
            result.error = OwnershipError::BadCount;
            return result;
        }

        Collection current;
        if (!LoadLocked(accountId, current))
            return result; // Unavailable

        m.before = current.Get(m.cardId);
        result.error = PlanGrant(rule, m.before, count, mode, m.after);
        if (result.error != OwnershipError::None)
        {
            result.status = MutationStatus::Rejected;
            result.mutation = m;
            return result;
        }
        m.reason = reason;
        m.source = ledgerSource;
        return CommitLocked(accountId, current, m, via);
    }

    MutationResult CollectionService::Remove(uint32_t accountId, std::string const& id, uint32_t count, bool all,
                                             std::string const& reason, std::string const& ledgerSource)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        MutationResult result;
        if (!m_catalog)
            return result; // Unavailable

        Mutation m;
        m.cardId = m_catalog->Resolve(id);
        if (m.cardId.empty())
        {
            result.status = MutationStatus::Rejected;
            result.error = OwnershipError::UnknownCard;
            return result;
        }
        if (!all && (count < 1 || count > kMaxCountPerOp))
        {
            result.status = MutationStatus::Rejected;
            result.error = OwnershipError::BadCount;
            return result;
        }

        Collection current;
        if (!LoadLocked(accountId, current))
            return result; // Unavailable

        m.before = current.Get(m.cardId);
        result.error = PlanRemove(m.before, count, all, m.after);
        if (result.error != OwnershipError::None)
        {
            result.status = MutationStatus::Rejected;
            result.mutation = m;
            return result;
        }
        m.reason = reason;
        m.source = ledgerSource;
        return CommitLocked(accountId, current, m);
    }

    TopUpResult CollectionService::TopUpProtected(uint32_t accountId, std::map<std::string, uint32_t> const& need,
                                                  std::string const& reason, std::string const& ledgerSource)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        TopUpResult result;
        if (!m_catalog)
            return result; // Unavailable

        Collection current;
        if (!LoadLocked(accountId, current))
            return result; // Unavailable

        std::vector<Mutation> mutations;
        uint32_t added = 0;
        for (auto const& entry : need)
        {
            Mutation m;
            CardRule rule;
            // Starter recipes hold standard cards only: the Quest source refuses anything else.
            result.error = ValidateGrant(*m_catalog, entry.first, GrantSource::Quest, m.cardId, rule);
            if (result.error == OwnershipError::None && (entry.second < 1 || entry.second > kMaxCountPerOp))
                result.error = OwnershipError::BadCount;
            if (result.error != OwnershipError::None)
            {
                result.status = MutationStatus::Rejected;
                result.card = entry.first;
                return result;
            }
            m.before = current.Get(m.cardId);
            if (m.before.protectedQty >= entry.second)
                continue;
            uint32_t const deficit = entry.second - m.before.protectedQty;
            result.error = PlanGrant(rule, m.before, deficit, ProtectionMode::Protected, m.after);
            if (result.error != OwnershipError::None)
            {
                result.status = MutationStatus::Rejected;
                result.card = m.cardId;
                return result;
            }
            m.reason = reason;
            m.source = ledgerSource;
            added += deficit;
            mutations.push_back(m);
        }

        result.status = MutationStatus::Ok;
        if (mutations.empty())
            return result;

        for (Mutation& m : mutations)
        {
            m.versionBefore = current.version;
            m.versionAfter = current.version + 1;
        }
        if (!m_storage.CommitCards(accountId, mutations))
        {
            m_cache.erase(accountId);
            result.status = MutationStatus::CommitFailed;
            result.mutations = mutations;
            return result;
        }
        current.version += 1;
        for (Mutation const& m : mutations)
            current.Set(m.cardId, m.after);
        if (m_online.count(accountId))
            m_cache[accountId] = current;
        else
            m_cache.erase(accountId);
        result.mutations = std::move(mutations);
        result.added = added;
        return result;
    }

    bool CollectionService::GetPacks(uint32_t accountId, PackView& out)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_catalog)
            return false;
        return LoadPacksLocked(accountId, out);
    }

    PackStockResult CollectionService::CommitStockLocked(uint32_t accountId, PackView const& current,
                                                         PackState const& after, std::string const& reason,
                                                         std::string const& ledgerSource, PackCommitFn const* via)
    {
        PackStockResult result;
        result.before = current.state;
        result.after = after;

        PackStockChange change;
        change.before = current.state;
        change.after = after;
        change.reason = reason;
        change.source = ledgerSource;
        if (!(via ? (*via)(change) : m_storage.CommitPackStock(accountId, change)))
        {
            m_packs.erase(accountId);
            result.status = PackStatus::CommitFailed;
            return result;
        }
        if (m_online.count(accountId))
        {
            PackView updated = current;
            updated.state = after;
            m_packs[accountId] = updated;
        }
        else
            m_packs.erase(accountId);
        result.status = PackStatus::Ok;
        return result;
    }

    PackStockResult CollectionService::GrantPacks(uint32_t accountId, uint32_t count, std::string const& reason,
                                                  std::string const& ledgerSource)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return GrantPacksLocked(accountId, count, reason, ledgerSource, nullptr);
    }

    PackStockResult CollectionService::GrantPacksVia(uint32_t accountId, uint32_t count, std::string const& reason,
                                                     std::string const& ledgerSource, PackCommitFn const& commit)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return GrantPacksLocked(accountId, count, reason, ledgerSource, &commit);
    }

    PackStockResult CollectionService::GrantPacksLocked(uint32_t accountId, uint32_t count, std::string const& reason,
                                                        std::string const& ledgerSource, PackCommitFn const* via)
    {
        PackStockResult result;
        if (!m_catalog)
            return result; // Unavailable
        if (count < 1 || count > kMaxPacksPerOp)
        {
            result.status = PackStatus::BadCount;
            return result;
        }
        PackView current;
        if (!LoadPacksLocked(accountId, current))
            return result; // Unavailable
        result.before = current.state;
        if (current.state.stock > kMaxPackStock || count > kMaxPackStock - current.state.stock)
        {
            result.status = PackStatus::Overflow;
            result.after = current.state;
            return result;
        }
        PackState after = current.state;
        after.stock += count;
        return CommitStockLocked(accountId, current, after, reason, ledgerSource, via);
    }

    PackStockResult CollectionService::RemovePacks(uint32_t accountId, uint32_t count, bool all,
                                                   std::string const& reason, std::string const& ledgerSource)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        PackStockResult result;
        if (!m_catalog)
            return result; // Unavailable
        if (!all && (count < 1 || count > kMaxPacksPerOp))
        {
            result.status = PackStatus::BadCount;
            return result;
        }
        PackView current;
        if (!LoadPacksLocked(accountId, current))
            return result; // Unavailable
        result.before = current.state;
        result.after = current.state;
        uint32_t const take = all ? current.state.stock : count;
        if (take == 0 || take > current.state.stock)
        {
            result.status = PackStatus::NotEnough;
            return result;
        }
        PackState after = current.state;
        after.stock -= take;
        return CommitStockLocked(accountId, current, after, reason, ledgerSource);
    }

    PackOpenResult CollectionService::OpenPack(uint32_t accountId, std::string const& key,
                                               std::string const& ledgerSource)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        PackOpenResult result;
        if (!m_catalog || !m_pool || !m_pool->Complete())
            return result; // Unavailable
        if (!IsValidPackKey(key))
        {
            result.status = PackStatus::BadKey;
            return result;
        }

        Collection collection;
        PackView packs;
        if (!LoadLocked(accountId, collection) || !LoadPacksLocked(accountId, packs))
            return result; // Unavailable

        // Idempotency: a key the account already used returns its opening.
        for (PackOpening const& o : packs.recent)
        {
            if (o.key == key)
            {
                result.status = PackStatus::Replay;
                result.opening = o;
                result.stockNow = packs.state.stock;
                return result;
            }
        }
        bool found = false;
        PackOpening stored;
        if (!m_storage.FindPackOpening(accountId, key, found, stored))
            return result; // Unavailable
        if (found)
        {
            result.status = PackStatus::Replay;
            result.opening = stored;
            result.stockNow = packs.state.stock;
            return result;
        }

        if (packs.state.stock == 0)
        {
            result.status = PackStatus::NoStock;
            return result;
        }

        PackRoll roll;
        if (!RollPack(*m_pool, m_odds, packs.state.sinceLegendary, m_rng, roll))
            return result; // Unavailable (incomplete pool, checked above)

        PackOpenCommit commit;
        commit.before = packs.state;
        commit.after = packs.state;
        commit.after.stock -= 1;
        commit.after.opened = packs.state.opened + 1;
        commit.after.sinceLegendary = SinceLegendaryAfter(roll, packs.state.sinceLegendary);
        commit.versionBefore = collection.version;
        commit.versionAfter = collection.version + 1;
        commit.source = ledgerSource;

        std::string const cardSource = std::string("pack:") + kPackTypeClassic + ":" + std::to_string(commit.after.opened);
        std::map<std::string, uint32_t> copies; // distinct cards of the pack, in id order
        for (std::string const& id : roll.cards)
            ++copies[id];
        for (auto const& entry : copies)
        {
            Mutation m;
            CardRule rule;
            OwnershipError error = ValidateGrant(*m_catalog, entry.first, GrantSource::Pack, m.cardId, rule);
            m.before = collection.Get(m.cardId);
            if (error == OwnershipError::None)
                error = PlanGrant(rule, m.before, entry.second, ProtectionMode::Unprotected, m.after);
            if (error != OwnershipError::None)
            {
                result.status = PackStatus::Refused;
                result.error = error;
                result.card = entry.first;
                return result;
            }
            m.versionBefore = commit.versionBefore;
            m.versionAfter = commit.versionAfter;
            m.reason = "pack_open";
            m.source = cardSource;
            commit.cards.push_back(m);
        }

        PackOpening& opening = commit.opening;
        opening.seq = commit.after.opened;
        opening.key = key;
        opening.cards = roll.cards;
        opening.isNew = NewFlags(collection, roll.cards);
        opening.collectionVersion = commit.versionAfter;
        opening.stockAfter = commit.after.stock;
        opening.sinceLegendaryBefore = commit.before.sinceLegendary;
        opening.sinceLegendaryAfter = commit.after.sinceLegendary;
        opening.pityApplied = roll.pityApplied;

        if (!m_storage.CommitPackOpen(accountId, commit))
        {
            // The database state is unknown now: read both again next time. A
            // retry with the same key finds the opening if it did commit.
            DropLocked(accountId);
            result.status = PackStatus::CommitFailed;
            return result;
        }

        if (m_online.count(accountId))
        {
            collection.version = commit.versionAfter;
            for (Mutation const& m : commit.cards)
                collection.Set(m.cardId, m.after);
            m_cache[accountId] = collection;
            packs.state = commit.after;
            packs.recent.insert(packs.recent.begin(), opening);
            if (packs.recent.size() > kRecentOpenings)
                packs.recent.resize(kRecentOpenings);
            m_packs[accountId] = packs;
        }
        else
            DropLocked(accountId);

        result.status = PackStatus::Ok;
        result.opening = opening;
        result.stockNow = commit.after.stock;
        result.roll = roll;
        return result;
    }

    // ---- economy ---------------------------------------------------------------------------------

    char const* WalletStatusText(WalletStatus s)
    {
        switch (s)
        {
            case WalletStatus::Ok: return "ok";
            case WalletStatus::NotEnough: return "not enough dust";
            case WalletStatus::Overflow: return "an account may hold at most 1000000000 dust";
            case WalletStatus::BadCount: return "nothing to change";
            case WalletStatus::Unavailable: return "wallet unavailable";
            case WalletStatus::CommitFailed: return "commit failed";
        }
        return "error";
    }

    char const* CraftStatusText(CraftStatus s)
    {
        switch (s)
        {
            case CraftStatus::Ok: return "ok";
            case CraftStatus::Replay: return "already done with this key";
            case CraftStatus::Refused: return "refused";
            case CraftStatus::BadKey: return "bad craft key";
            case CraftStatus::Unavailable: return "crafting unavailable";
            case CraftStatus::CommitFailed: return "commit failed";
        }
        return "error";
    }

    void CollectionService::SetDustValues(DustValues values)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_dust = SanitizeDustValues(values);
    }

    DustValues CollectionService::GetDustValues() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_dust;
    }

    bool CollectionService::IsWalletCached(uint32_t accountId) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_wallets.count(accountId) != 0;
    }

    bool CollectionService::LoadWalletLocked(uint32_t accountId, Wallet& out)
    {
        auto const it = m_wallets.find(accountId);
        if (it != m_wallets.end())
        {
            out = it->second;
            return true;
        }
        Wallet loaded;
        if (!m_storage.LoadWallet(accountId, loaded))
            return false;
        if (m_online.count(accountId))
            m_wallets[accountId] = loaded;
        out = loaded;
        return true;
    }

    bool CollectionService::GetWallet(uint32_t accountId, Wallet& out)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_catalog)
            return false;
        return LoadWalletLocked(accountId, out);
    }

    WalletResult CollectionService::ChangeDust(uint32_t accountId, int64_t delta, std::string const& reason,
                                               std::string const& ledgerSource)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        WalletResult result;
        if (!m_catalog)
            return result; // Unavailable
        if (delta == 0)
        {
            result.status = WalletStatus::BadCount;
            return result;
        }
        Wallet current;
        if (!LoadWalletLocked(accountId, current))
            return result; // Unavailable
        result.before = current;
        result.after = current;
        int64_t const dust = static_cast<int64_t>(current.dust) + delta;
        if (dust < 0)
        {
            result.status = WalletStatus::NotEnough;
            return result;
        }
        if (dust > static_cast<int64_t>(kMaxBalance))
        {
            result.status = WalletStatus::Overflow;
            return result;
        }
        WalletChange change;
        change.before = current;
        change.after.dust = static_cast<uint32_t>(dust);
        change.reason = reason;
        change.source = ledgerSource;
        if (!m_storage.CommitWallet(accountId, change))
        {
            m_wallets.erase(accountId);
            result.status = WalletStatus::CommitFailed;
            return result;
        }
        if (m_online.count(accountId))
            m_wallets[accountId] = change.after;
        else
            m_wallets.erase(accountId);
        result.after = change.after;
        result.status = WalletStatus::Ok;
        return result;
    }

    bool CollectionService::BeginCraftLocked(uint32_t accountId, std::string const& key, Collection& collection,
                                             Wallet& wallet, CraftResult& result)
    {
        if (!m_catalog)
            return false; // Unavailable
        if (!IsValidCraftKey(key))
        {
            result.status = CraftStatus::BadKey;
            return false;
        }
        if (!LoadLocked(accountId, collection) || !LoadWalletLocked(accountId, wallet))
            return false; // Unavailable
        bool found = false;
        CraftRecord stored;
        if (!m_storage.FindCraftOp(accountId, key, found, stored))
            return false; // Unavailable
        if (found)
        {
            result.status = CraftStatus::Replay;
            result.record = stored;
            result.wallet = wallet;
            return false;
        }
        result.wallet = wallet;
        return true;
    }

    CraftResult CollectionService::CommitCraftLocked(uint32_t accountId, Collection& collection, Wallet const& wallet,
                                                     CraftCommit& commit)
    {
        CraftResult result;
        commit.card.versionBefore = collection.version;
        commit.card.versionAfter = collection.version + 1;
        commit.record.collectionVersion = commit.card.versionAfter;
        commit.record.cardAfter = commit.card.after;
        commit.record.dustAfter = commit.wallet.after.dust;
        commit.wallet.before = wallet;
        result.before = commit.card.before;
        result.wallet = wallet;
        if (!m_storage.CommitCraft(accountId, commit))
        {
            // The database state is unknown now: read everything again. A retry with the same key finds the
            // craft if it did commit.
            DropLocked(accountId);
            result.status = CraftStatus::CommitFailed;
            return result;
        }
        collection.version = commit.card.versionAfter;
        collection.Set(commit.card.cardId, commit.card.after);
        if (m_online.count(accountId))
        {
            m_cache[accountId] = collection;
            m_wallets[accountId] = commit.wallet.after;
        }
        else
            DropLocked(accountId);
        result.status = CraftStatus::Ok;
        result.record = commit.record;
        result.wallet = commit.wallet.after;
        return result;
    }

    CraftResult CollectionService::Craft(uint32_t accountId, std::string const& key, std::string const& cardId,
                                         std::string const& ledgerSource)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        CraftResult result;
        Collection collection;
        Wallet wallet;
        if (!BeginCraftLocked(accountId, key, collection, wallet, result))
            return result;

        result.status = CraftStatus::Refused;
        std::string const id = m_catalog->Resolve(cardId);
        CardRule const* rule = id.empty() ? nullptr : m_catalog->Find(id);
        std::size_t bucket = 0;
        if (!rule || !IsCraftable(*rule) || !DustBucket(rule->rarity, bucket))
        {
            result.why = CraftWhy::Card;
            return result;
        }
        CraftCommit commit;
        commit.card.cardId = id;
        commit.card.before = collection.Get(id);
        if (commit.card.before.Total() >= CraftLimit(*rule))
        {
            result.why = CraftWhy::Limit;
            return result;
        }
        uint32_t const cost = m_dust.craft[bucket];
        if (wallet.dust < cost)
        {
            result.why = CraftWhy::Dust;
            result.needed = cost;
            return result;
        }
        if (PlanGrant(*rule, commit.card.before, 1, ProtectionMode::Unprotected, commit.card.after) != OwnershipError::None)
        {
            result.why = CraftWhy::Limit;
            return result;
        }
        commit.card.reason = "craft";
        commit.card.source = ledgerSource;
        commit.wallet.after = wallet;
        commit.wallet.after.dust = wallet.dust - cost;
        commit.wallet.reason = "craft";
        commit.wallet.source = ledgerSource;
        commit.record.key = key;
        commit.record.kind = CraftKind::Make;
        commit.record.cardId = id;
        commit.record.count = 1;
        commit.record.dustDelta = -static_cast<int32_t>(cost);
        return CommitCraftLocked(accountId, collection, wallet, commit);
    }

    CraftResult CollectionService::Scrap(uint32_t accountId, std::string const& key, std::string const& cardId,
                                         uint32_t count, std::string const& ledgerSource)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        CraftResult result;
        Collection collection;
        Wallet wallet;
        if (!BeginCraftLocked(accountId, key, collection, wallet, result))
            return result;

        result.status = CraftStatus::Refused;
        if (count < 1 || count > kMaxScrapPerOp)
        {
            result.why = CraftWhy::Count;
            return result;
        }
        std::string const id = m_catalog->Resolve(cardId);
        CardRule const* rule = id.empty() ? nullptr : m_catalog->Find(id);
        std::size_t bucket = 0;
        if (!rule || !IsCraftable(*rule) || !DustBucket(rule->rarity, bucket))
        {
            result.why = CraftWhy::Card;
            return result;
        }
        CraftCommit commit;
        commit.card.cardId = id;
        commit.card.before = collection.Get(id);
        // Only unprotected copies: starter copies and the quest copies that fill a playable set never.
        if (commit.card.before.unprotectedQty < count)
        {
            result.why = CraftWhy::Copies;
            return result;
        }
        uint64_t const gain = static_cast<uint64_t>(m_dust.scrap[bucket]) * count;
        if (static_cast<uint64_t>(wallet.dust) + gain > kMaxBalance)
        {
            result.why = CraftWhy::Limit;
            return result;
        }
        commit.card.after = commit.card.before;
        commit.card.after.unprotectedQty -= count;
        commit.card.reason = "scrap";
        commit.card.source = ledgerSource;
        commit.wallet.after = wallet;
        commit.wallet.after.dust = wallet.dust + static_cast<uint32_t>(gain);
        commit.wallet.reason = "scrap";
        commit.wallet.source = ledgerSource;
        commit.record.key = key;
        commit.record.kind = CraftKind::Scrap;
        commit.record.cardId = id;
        commit.record.count = count;
        commit.record.dustDelta = static_cast<int32_t>(gain);
        return CommitCraftLocked(accountId, collection, wallet, commit);
    }

    CraftResult CollectionService::ScrapExtras(uint32_t accountId, std::string const& key,
                                               std::string const& ledgerSource)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        CraftResult result;
        Collection collection;
        Wallet wallet;
        if (!BeginCraftLocked(accountId, key, collection, wallet, result))
            return result;

        result.status = CraftStatus::Refused;
        CraftExtrasCommit commit;
        uint64_t copies = 0;
        uint64_t gain = 0;
        for (auto const& entry : collection.cards) // sorted by id: the same collection always plans the same op
        {
            CardRule const* rule = m_catalog->Find(entry.first);
            std::size_t bucket = 0;
            if (!rule || !IsCraftable(*rule) || !DustBucket(rule->rarity, bucket))
                continue;
            CardCount const& have = entry.second;
            uint32_t const limit = CraftLimit(*rule);
            if (have.Total() <= limit)
                continue;
            // Protected copies (starter and quest copies) are never scrapped, so at most the unprotected ones.
            uint32_t const extra = std::min(have.unprotectedQty, have.Total() - limit);
            if (extra == 0)
                continue;
            if (copies + extra > kMaxExtrasPerOp)
                break;
            Mutation m;
            m.cardId = entry.first;
            m.before = have;
            m.after = have;
            m.after.unprotectedQty -= extra;
            m.reason = "scrap";
            m.source = ledgerSource;
            commit.cards.push_back(m);
            copies += extra;
            gain += static_cast<uint64_t>(m_dust.scrap[bucket]) * extra;
        }
        if (commit.cards.empty())
        {
            result.why = CraftWhy::Nothing;
            return result;
        }
        if (static_cast<uint64_t>(wallet.dust) + gain > kMaxBalance)
        {
            result.why = CraftWhy::Limit;
            return result;
        }
        uint32_t const versionAfter = collection.version + 1;
        for (Mutation& m : commit.cards)
        {
            m.versionBefore = collection.version;
            m.versionAfter = versionAfter;
        }
        commit.wallet.before = wallet;
        commit.wallet.after = wallet;
        commit.wallet.after.dust = wallet.dust + static_cast<uint32_t>(gain);
        commit.wallet.reason = "scrap";
        commit.wallet.source = ledgerSource;
        commit.record.key = key;
        commit.record.kind = CraftKind::Extras;
        commit.record.cardId = kExtrasCardId;
        commit.record.count = static_cast<uint32_t>(copies);
        commit.record.dustDelta = static_cast<int32_t>(gain);
        commit.record.dustAfter = commit.wallet.after.dust;
        commit.record.collectionVersion = versionAfter;

        result.wallet = wallet;
        if (!m_storage.CommitCraftExtras(accountId, commit))
        {
            // The database state is unknown now: read everything again. A retry with the same key finds the op
            // if it did commit.
            DropLocked(accountId);
            result.status = CraftStatus::CommitFailed;
            return result;
        }
        collection.version = versionAfter;
        for (Mutation const& m : commit.cards)
            collection.Set(m.cardId, m.after);
        if (m_online.count(accountId))
        {
            m_cache[accountId] = collection;
            m_wallets[accountId] = commit.wallet.after;
        }
        else
            DropLocked(accountId);
        result.status = CraftStatus::Ok;
        result.record = commit.record;
        result.wallet = commit.wallet.after;
        return result;
    }
}
