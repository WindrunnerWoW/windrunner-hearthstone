#include "HsDeckService.h"

#include <algorithm>
#include <utility>

namespace Hs
{
    std::set<std::string> DeckBook::UnlockedClasses() const
    {
        std::set<std::string> classes;
        for (auto const& entry : unlocks)
            classes.insert(entry.first);
        return classes;
    }

    char const* DeckActionName(DeckAction a)
    {
        switch (a)
        {
            case DeckAction::Create: return "create";
            case DeckAction::Update: return "update";
            case DeckAction::Delete: return "delete";
        }
        return "update";
    }

    char const* DeckStatusText(DeckStatus s)
    {
        switch (s)
        {
            case DeckStatus::Ok: return "ok";
            case DeckStatus::Replay: return "already done with this key";
            case DeckStatus::Refused: return "refused";
            case DeckStatus::BadKey: return "bad deck key";
            case DeckStatus::Unavailable: return "decks unavailable";
            case DeckStatus::CommitFailed: return "commit failed";
        }
        return "error";
    }

    char const* ClassStatusText(ClassStatus s)
    {
        switch (s)
        {
            case ClassStatus::Ok: return "ok";
            case ClassStatus::BadClass: return "not a class (DRUID HUNTER MAGE PALADIN PRIEST ROGUE SHAMAN WARLOCK WARRIOR)";
            case ClassStatus::Already: return "the class is already unlocked for the account";
            case ClassStatus::NotUnlocked: return "the class is not unlocked for the account";
            case ClassStatus::Unavailable: return "class access unavailable";
            case ClassStatus::CommitFailed: return "commit failed";
        }
        return "error";
    }

    char const* StarterStatusText(StarterStatus s)
    {
        switch (s)
        {
            case StarterStatus::Ok: return "ok";
            case StarterStatus::Already: return "the account already received this starter deck";
            case StarterStatus::Refused: return "the starter recipe breaks a deck rule";
            case StarterStatus::Unavailable: return "decks unavailable";
            case StarterStatus::CommitFailed: return "commit failed";
        }
        return "error";
    }

    std::string DeckMatchCheck::Text() const
    {
        if (status != Status::Ok)
            return status == Status::Missing ? "missing" : "unavailable";
        if (Ready())
            return "ready";
        std::string s = state.Playable() ? std::string() : state.Text();
        if (engine)
            s += s.empty() ? "engine" : ",engine";
        return s;
    }

    DeckService::DeckService(DeckStorage& storage, CollectionService& collections)
        : m_storage(storage), m_collections(collections)
    {
    }

    void DeckService::SetMaxSaved(uint32_t maxSaved)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_maxSaved = std::min(std::max<uint32_t>(maxSaved, 1), kMaxSavedDecksLimit);
    }

    uint32_t DeckService::MaxSaved() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_maxSaved;
    }

    void DeckService::SetEngineSupport(EngineSupport predicate)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_engine = std::move(predicate);
    }

    bool DeckService::HasEngineSupport() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return static_cast<bool>(m_engine);
    }

    void DeckService::SetOnline(uint32_t accountId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_online.insert(accountId);
        m_cache.erase(accountId); // a fresh login reads the database again
    }

    void DeckService::SetOffline(uint32_t accountId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_online.erase(accountId);
        m_cache.erase(accountId);
    }

    void DeckService::Evict(uint32_t accountId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cache.erase(accountId);
    }

    bool DeckService::IsOnline(uint32_t accountId) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_online.count(accountId) != 0;
    }

    std::size_t DeckService::CachedCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_cache.size();
    }

    bool DeckService::IsCached(uint32_t accountId) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_cache.count(accountId) != 0;
    }

    bool DeckService::LoadLocked(uint32_t accountId, DeckBook& out)
    {
        auto const it = m_cache.find(accountId);
        if (it != m_cache.end())
        {
            out = it->second;
            return true;
        }
        DeckBook loaded;
        if (!m_storage.LoadBook(accountId, loaded))
            return false;
        for (auto const& entry : loaded.decks)
            loaded.maxDeckId = std::max(loaded.maxDeckId, entry.first);
        if (m_online.count(accountId))
            m_cache[accountId] = loaded;
        out = std::move(loaded);
        return true;
    }

    void DeckService::StoreLocked(uint32_t accountId, DeckBook const& book)
    {
        if (m_online.count(accountId))
            m_cache[accountId] = book;
        else
            m_cache.erase(accountId);
    }

    bool DeckService::GetBook(uint32_t accountId, DeckBook& out)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return LoadLocked(accountId, out);
    }

    bool DeckService::List(uint32_t accountId, std::string const& startClass, DeckList& out)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::shared_ptr<CardCatalog const> const catalog = m_collections.Catalog();
        DeckBook book;
        Collection collection;
        if (!catalog || !LoadLocked(accountId, book) || !m_collections.Get(accountId, collection))
            return false;

        std::set<std::string> const unlocked = book.UnlockedClasses();
        std::set<std::string> const playable = PlayableClasses(startClass, unlocked);
        DeckList list;
        list.cap = m_maxSaved;
        list.start = startClass;
        list.unlocked.assign(unlocked.begin(), unlocked.end());
        for (auto const& entry : book.decks) // std::map: by id
        {
            DeckListEntry e;
            e.deck = entry.second;
            e.state = EvaluateDeck(*catalog, collection, e.deck, startClass.empty() ? nullptr : &playable).Text();
            list.decks.push_back(std::move(e));
        }
        out = std::move(list);
        return true;
    }

    DeckSaveResult DeckService::SaveDeck(uint32_t accountId, DeckSaveRequest const& req, std::string const& startClass)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        DeckSaveResult result;
        std::shared_ptr<CardCatalog const> const catalog = m_collections.Catalog();
        if (!catalog)
            return result; // Unavailable
        if (!req.key.empty() && !IsValidDeckKey(req.key))
        {
            result.status = DeckStatus::BadKey;
            return result;
        }

        // 1. The payload and the hard card rules (no database access).
        Deck deck;
        deck.deckClass = req.deckClass;
        deck.cards = req.cards;
        deck.origin = req.origin;
        if (!IsDeckClass(req.deckClass))
            result.why = DeckWhy::Class;
        else if (!NormalizeDeckName(req.name, deck.name))
            result.why = DeckWhy::Name;
        else
            result.why = ValidateDeckCards(*catalog, req.deckClass, req.cards);
        if (result.why != DeckWhy::None)
        {
            result.status = DeckStatus::Refused;
            return result;
        }

        // 2. Replay: a key the account already used.
        bool found = false;
        DeckOp stored;
        if (!req.key.empty() && !m_storage.FindDeckOp(accountId, req.key, found, stored))
            return result; // Unavailable

        // 3. The book and the collection (for the state of the reply).
        DeckBook book;
        Collection collection;
        if (!LoadLocked(accountId, book) || !m_collections.Get(accountId, collection))
            return result; // Unavailable
        std::set<std::string> const playable = PlayableClasses(startClass, book.UnlockedClasses());
        std::set<std::string> const* asking = startClass.empty() ? nullptr : &playable;

        if (found)
        {
            // The same save again: of the same deck, or the create (rev 1) that made it.
            bool const same = stored.op == kDeckOpSave &&
                (req.id == 0 ? stored.rev == 1 : req.id == stored.deckId);
            if (!same)
            {
                result.status = DeckStatus::Refused;
                result.why = DeckWhy::Key;
                return result;
            }
            result.status = DeckStatus::Replay;
            result.id = stored.deckId;
            result.rev = stored.rev;
            auto const it = book.decks.find(stored.deckId);
            result.state = it == book.decks.end() ? std::string("gone")
                : EvaluateDeck(*catalog, collection, it->second, asking).Text();
            return result;
        }

        // 4. Revision and cap.
        DeckCommit commit;
        if (req.id == 0)
        {
            // bypassCap passes the configured cap, never the DECKS reply bound.
            if ((book.decks.size() >= m_maxSaved && !req.bypassCap) || book.decks.size() >= kMaxListedDecks)
            {
                result.status = DeckStatus::Refused;
                result.why = DeckWhy::Cap;
                return result;
            }
            commit.action = DeckAction::Create;
            deck.id = book.maxDeckId + 1;
            deck.rev = 1;
        }
        else
        {
            auto const it = book.decks.find(req.id);
            if (it == book.decks.end())
                result.why = DeckWhy::Missing;
            else if (it->second.deckClass != req.deckClass)
                result.why = DeckWhy::Class; // the class is fixed at creation
            else if (it->second.rev != req.baseRev)
                result.why = DeckWhy::Conflict;
            if (result.why != DeckWhy::None)
            {
                result.status = DeckStatus::Refused;
                return result;
            }
            commit.action = DeckAction::Update;
            commit.revBefore = it->second.rev;
            deck.id = req.id;
            deck.rev = it->second.rev + 1;
            deck.origin = it->second.origin; // an edit keeps where the deck came from
        }

        // 5. One commit: deck row, op row, ledger row.
        commit.deck = deck;
        if (!req.key.empty())
        {
            commit.op.key = req.key;
            commit.op.op = kDeckOpSave;
            commit.op.deckId = deck.id;
            commit.op.rev = deck.rev;
        }
        commit.reason = req.reason;
        commit.source = req.source;
        if (!m_storage.CommitDeck(accountId, commit))
        {
            // The database state is unknown now: read it again next time. A
            // retry with the same key replays if the commit did happen.
            m_cache.erase(accountId);
            result.status = DeckStatus::CommitFailed;
            return result;
        }

        // 6. The cache.
        book.decks[deck.id] = deck;
        book.maxDeckId = std::max(book.maxDeckId, deck.id);
        StoreLocked(accountId, book);

        result.status = DeckStatus::Ok;
        result.created = commit.action == DeckAction::Create;
        result.deck = deck;
        result.id = deck.id;
        result.rev = deck.rev;
        result.state = EvaluateDeck(*catalog, collection, deck, asking).Text();
        return result;
    }

    DeckDeleteResult DeckService::DeleteDeck(uint32_t accountId, DeckDeleteRequest const& req)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        DeckDeleteResult result;
        result.id = req.id;
        if (!m_collections.Catalog())
            return result; // Unavailable, like every other deck operation
        if (!req.key.empty() && !IsValidDeckKey(req.key))
        {
            result.status = DeckStatus::BadKey;
            return result;
        }

        bool found = false;
        DeckOp stored;
        if (!req.key.empty() && !m_storage.FindDeckOp(accountId, req.key, found, stored))
            return result; // Unavailable
        if (found)
        {
            if (stored.op != kDeckOpDelete || stored.deckId != req.id)
            {
                result.status = DeckStatus::Refused;
                result.why = DeckWhy::Key;
                return result;
            }
            result.status = DeckStatus::Replay;
            return result;
        }

        DeckBook book;
        if (!LoadLocked(accountId, book))
            return result; // Unavailable
        auto const it = book.decks.find(req.id);
        if (it == book.decks.end())
            result.why = DeckWhy::Missing;
        else if (!req.anyRev && it->second.rev != req.baseRev)
            result.why = DeckWhy::Conflict;
        if (result.why != DeckWhy::None)
        {
            result.status = DeckStatus::Refused;
            return result;
        }

        DeckCommit commit;
        commit.action = DeckAction::Delete;
        commit.deck = it->second;
        commit.revBefore = it->second.rev;
        commit.deck.rev = it->second.rev + 1;
        if (!req.key.empty())
        {
            commit.op.key = req.key;
            commit.op.op = kDeckOpDelete;
            commit.op.deckId = req.id;
            commit.op.rev = commit.deck.rev;
        }
        commit.reason = req.reason;
        commit.source = req.source;
        if (!m_storage.CommitDeck(accountId, commit))
        {
            m_cache.erase(accountId);
            result.status = DeckStatus::CommitFailed;
            return result;
        }

        book.decks.erase(req.id); // maxDeckId stays: the id is never reused
        StoreLocked(accountId, book);
        result.status = DeckStatus::Ok;
        result.deck = commit.deck;
        return result;
    }

    ClassChangeResult DeckService::ChangeClassLocked(uint32_t accountId, std::string const& cls, bool unlock,
                                                     std::string const& reason, std::string const& source)
    {
        ClassChangeResult result;
        result.cls = cls;
        if (!IsDeckClass(cls))
        {
            result.status = ClassStatus::BadClass;
            return result;
        }
        DeckBook book;
        if (!LoadLocked(accountId, book))
            return result; // Unavailable
        bool const has = book.unlocks.count(cls) != 0;
        if (unlock && has)
        {
            result.status = ClassStatus::Already;
            return result;
        }
        if (!unlock && !has)
        {
            result.status = ClassStatus::NotUnlocked;
            return result;
        }

        ClassCommit commit;
        commit.cls = cls;
        commit.unlock = unlock;
        commit.reason = reason;
        commit.source = source;
        if (!m_storage.CommitClass(accountId, commit))
        {
            m_cache.erase(accountId);
            result.status = ClassStatus::CommitFailed;
            return result;
        }
        if (unlock)
            book.unlocks[cls] = source;
        else
            book.unlocks.erase(cls);
        StoreLocked(accountId, book);
        result.status = ClassStatus::Ok;
        return result;
    }

    StarterAwardResult DeckService::AwardStarter(uint32_t accountId, StarterAwardRequest const& req)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        StarterAwardResult result;
        std::shared_ptr<CardCatalog const> const catalog = m_collections.Catalog();
        if (!catalog)
            return result; // Unavailable

        Deck deck;
        deck.deckClass = req.cls;
        deck.cards = req.cards;
        deck.origin = kDeckOriginStarter;
        if (!IsDeckClass(req.cls))
            result.why = DeckWhy::Class;
        else if (!NormalizeDeckName(req.name, deck.name))
            result.why = DeckWhy::Name;
        else
            result.why = ValidateDeckCards(*catalog, req.cls, req.cards);
        if (result.why == DeckWhy::None && deck.CardCount() != kDeckSize)
            result.why = DeckWhy::Size;
        if (result.why != DeckWhy::None)
        {
            result.status = StarterStatus::Refused;
            return result;
        }

        DeckBook book;
        if (!LoadLocked(accountId, book) || !book.startersKnown)
            return result; // Unavailable
        auto const had = book.starters.find(req.cls);
        if (had != book.starters.end())
        {
            result.status = StarterStatus::Already;
            result.deckId = had->second;
            return result;
        }

        StarterCommit commit;
        commit.cls = req.cls;
        commit.source = req.source;
        // The configured cap, not the DECKS reply bound: a full list gets the cards only (owner decision).
        commit.withDeck = book.decks.size() < m_maxSaved && book.decks.size() < kMaxListedDecks;
        if (commit.withDeck)
        {
            deck.id = book.maxDeckId + 1;
            deck.rev = 1;
            commit.deck.action = DeckAction::Create;
            commit.deck.deck = deck;
            commit.deck.reason = req.reason;
            commit.deck.source = req.source;
        }
        if (!m_storage.CommitStarter(accountId, commit))
        {
            m_cache.erase(accountId);
            result.status = StarterStatus::CommitFailed;
            return result;
        }

        book.starters[req.cls] = commit.withDeck ? deck.id : 0;
        if (commit.withDeck)
        {
            book.decks[deck.id] = deck;
            book.maxDeckId = std::max(book.maxDeckId, deck.id);
        }
        StoreLocked(accountId, book);
        result.status = StarterStatus::Ok;
        result.deckCreated = commit.withDeck;
        result.listFull = !commit.withDeck;
        if (commit.withDeck)
        {
            result.deck = deck;
            result.deckId = deck.id;
        }
        return result;
    }

    ClassChangeResult DeckService::UnlockClass(uint32_t accountId, std::string const& cls, std::string const& reason,
                                               std::string const& source)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return ChangeClassLocked(accountId, cls, true, reason, source);
    }

    ClassChangeResult DeckService::LockClass(uint32_t accountId, std::string const& cls, std::string const& reason,
                                             std::string const& source)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return ChangeClassLocked(accountId, cls, false, reason, source);
    }

    DeckMatchCheck DeckService::CheckForMatch(uint32_t accountId, uint32_t deckId, std::string const& startClass)
    {
        DeckMatchCheck check;
        EngineSupport engine;
        DeckCards cards;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            std::shared_ptr<CardCatalog const> const catalog = m_collections.Catalog();
            DeckBook book;
            Collection collection;
            if (!catalog || !LoadLocked(accountId, book) || !m_collections.Get(accountId, collection))
                return check; // Unavailable
            auto const it = book.decks.find(deckId);
            if (it == book.decks.end())
            {
                check.status = DeckMatchCheck::Status::Missing;
                return check;
            }
            std::set<std::string> const playable = PlayableClasses(startClass, book.UnlockedClasses());
            check.status = DeckMatchCheck::Status::Ok;
            check.state = EvaluateDeck(*catalog, collection, it->second, &playable);
            engine = m_engine;
            cards = it->second.cards;
        }
        // The engine predicate runs without the deck mutex, so it may call back
        // into this service without deadlocking.
        if (!engine)
            check.engine = true;
        else
        {
            for (auto const& entry : cards)
            {
                if (!engine(entry.first))
                {
                    check.engine = true;
                    check.unsupportedCard = entry.first;
                    break;
                }
            }
        }
        return check;
    }
}
