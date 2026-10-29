/*
 * Deck service: per-account deck book cache (decks + class unlocks), the
 * deck save / delete order (validate, replay check,
 * load, rev / cap checks, one commit, then update the cache), account class
 * unlocks and the match-start revalidation. Persistence is injected through
 * DeckStorage (database in the game, a fake in tests); card rules and the
 * account collection come from the CollectionService.
 *
 * PURE C++ (standard library only, no core headers).
 *
 * Threading: every public method takes the service mutex and keeps it across
 * load, commit and cache update. Lock order: DeckService mutex, then the
 * CollectionService mutex (Catalog(), Get()); the CollectionService never
 * calls the DeckService. Callers must not hold a lock that the storage or a
 * later DECK_CHANGED push takes; the glue pushes after the call returns.
 */

#ifndef MOD_HEARTHSTONE_HS_DECK_SERVICE_H
#define MOD_HEARTHSTONE_HS_DECK_SERVICE_H

#include "HsDecks.h"
#include "../collection/HsCollectionService.h"

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Hs
{
    // Live decks and class unlocks of one account.
    struct DeckBook
    {
        std::map<uint32_t, Deck> decks;                // live (not deleted) decks by id
        uint32_t maxDeckId = 0;                        // highest id ever used, deleted decks included
        std::map<std::string, std::string> unlocks;    // class -> source of the unlock
        // The classes whose starter deck was awarded (once per account), with the deck created by the
        // award (0: cards only, the deck list was full; the deck may have been deleted since).
        std::map<std::string, uint32_t> starters;
        bool startersKnown = true; // false: the starter table could not be read (awards answer Unavailable)
        std::set<std::string> UnlockedClasses() const;
    };

    // One idempotency record (hearthstone_deck_op row).
    struct DeckOp
    {
        std::string key;
        std::string op;      // kDeckOpSave or kDeckOpDelete
        uint32_t deckId = 0;
        uint32_t rev = 0;    // the deck's revision after the operation (1: it created the deck)
    };

    enum class DeckAction { Create, Update, Delete };
    char const* DeckActionName(DeckAction a); // "create", "update", "delete"

    // One committed deck change: the deck row, the op row (client operations
    // only, they carry a key) and a ledger row, all in one transaction.
    struct DeckCommit
    {
        DeckAction action = DeckAction::Create;
        Deck deck;              // the row afterwards (Delete: the deck with its final rev)
        uint32_t revBefore = 0; // 0 for a create
        DeckOp op;              // op.key empty: no op row
        std::string reason;     // ledger reason, for example deck_save, gm_fixture, gm_delete
        std::string source;     // ledger source, for example char:42, gm:5
    };

    // One class unlock or lock: the hearthstone_account_class row and a ledger row.
    struct ClassCommit
    {
        std::string cls;
        bool unlock = true;
        std::string reason; // for example gm_unlock, gm_lock
        std::string source; // for example gm:5 (stored as the unlock's source)
    };

    // One starter award: the hearthstone_account_starter row, plus the deck row and its ledger row when
    // withDeck, in one transaction.
    struct StarterCommit
    {
        std::string cls;
        bool withDeck = false;
        DeckCommit deck;    // withDeck: a Create
        std::string source; // stored with the starter row
    };

    class DeckStorage
    {
    public:
        virtual ~DeckStorage() = default;
        // Live decks, the highest deck id ever used and the class unlocks of an
        // account (unknown account: all empty). False on a database failure.
        virtual bool LoadBook(uint32_t accountId, DeckBook& out) = 0;
        // The op stored under key, if any. False on a database failure.
        virtual bool FindDeckOp(uint32_t accountId, std::string const& key, bool& found, DeckOp& out) = 0;
        // True only if the whole change committed. The store rejects a key the
        // account already used (primary key), so a race cannot apply twice.
        virtual bool CommitDeck(uint32_t accountId, DeckCommit const& c) = 0;
        // True only if the change committed.
        virtual bool CommitClass(uint32_t accountId, ClassCommit const& c) = 0;
        // True only if the whole award committed. The store rejects a class the account already has a starter row
        // for (primary key), so a race cannot award twice.
        virtual bool CommitStarter(uint32_t accountId, StarterCommit const& c) = 0;
    };

    enum class DeckStatus
    {
        Ok,
        Replay,       // the key was already used for this operation: its stored result, nothing changed
        Refused,      // a rule rejected the request (see why); nothing changed
        BadKey,       // key not [0-9a-z]{12,16}
        Unavailable,  // card data not loaded or the account could not be read; nothing changed
        CommitFailed  // the transaction did not commit (or its outcome is unknown); the cache was dropped
    };
    char const* DeckStatusText(DeckStatus s);

    struct DeckSaveRequest
    {
        std::string key;        // client operations: [0-9a-z]{12,16}; empty for GM / server saves (no op row)
        uint32_t id = 0;        // 0 = create
        uint32_t baseRev = 0;   // the revision the change is based on (update only)
        std::string deckClass;
        std::string name;       // raw, trimmed by the service
        DeckCards cards;
        std::string origin = kDeckOriginPlayer;
        std::string reason = "deck_save";
        std::string source;
        bool bypassCap = false; // passes the configured cap (never kMaxListedDecks); unused by the starter awards
    };

    struct DeckSaveResult
    {
        DeckStatus status = DeckStatus::Unavailable;
        DeckWhy why = DeckWhy::None; // Refused
        bool created = false;        // Ok: a new deck
        Deck deck;                   // Ok: the saved deck
        uint32_t id = 0;             // Ok, Replay
        uint32_t rev = 0;            // Ok, Replay (the stored revision of the op)
        std::string state;           // Ok, Replay: the deck's state now, "gone" if deleted since
    };

    struct DeckDeleteRequest
    {
        std::string key;      // empty for GM deletes (no op row)
        uint32_t id = 0;
        uint32_t baseRev = 0;
        bool anyRev = false;  // GM deletes skip the revision check
        std::string reason = "deck_delete";
        std::string source;
    };

    struct DeckDeleteResult
    {
        DeckStatus status = DeckStatus::Unavailable;
        DeckWhy why = DeckWhy::None;
        uint32_t id = 0;
        Deck deck; // Ok: the deleted deck (with its final revision)
    };

    enum class ClassStatus
    {
        Ok,
        BadClass,     // not one of the nine classes
        Already,      // unlock of an unlocked class
        NotUnlocked,  // lock of a class that is not unlocked
        Unavailable,
        CommitFailed
    };
    char const* ClassStatusText(ClassStatus s);

    struct ClassChangeResult
    {
        ClassStatus status = ClassStatus::Unavailable;
        std::string cls;
    };

    // Starter deck award: once per account and class. The deck is created only
    // when the account has room under the configured cap; with a full list the award is recorded as cards only and
    // the player builds the deck from the (protected) cards. The cards themselves are granted before, by
    // CollectionService::TopUpProtected (idempotent), so a retry after a failure between the two steps is safe.
    enum class StarterStatus
    {
        Ok,           // the award was recorded now (deckCreated or listFull)
        Already,      // the account already had this class's starter award: nothing changed
        Refused,      // the recipe breaks a rule (see why): nothing changed
        Unavailable,
        CommitFailed
    };
    char const* StarterStatusText(StarterStatus s);

    struct StarterAwardRequest
    {
        std::string cls;    // one of the nine classes
        std::string name;   // the deck name, for example "Basic Mage"
        DeckCards cards;    // the recipe: exactly kDeckSize cards
        std::string reason = "starter"; // deck ledger reason
        std::string source;             // for example char:42 or gm:5
    };

    struct StarterAwardResult
    {
        StarterStatus status = StarterStatus::Unavailable;
        DeckWhy why = DeckWhy::None; // Refused
        bool deckCreated = false;    // Ok: deck holds the new deck
        bool listFull = false;       // Ok: cards only, the deck list was at the cap
        Deck deck;
        uint32_t deckId = 0;         // Ok and Already: the deck of the award (0: none)
    };

    // Match-start revalidation of one deck for one character.
    struct DeckMatchCheck
    {
        enum class Status { Ok, Missing, Unavailable } status = Status::Unavailable;
        DeckState state;
        bool engine = false;           // a card is not supported by the match engine (or none is registered)
        std::string unsupportedCard;   // the first such card (empty without an engine)
        bool Ready() const { return status == Status::Ok && state.Playable() && !engine; }
        // "ready" or the reasons in the order size,owned,invalid,locked,engine.
        std::string Text() const;
    };

    // True if the match engine supports the card (registers it).
    using EngineSupport = std::function<bool(std::string const& cardId)>;

    class DeckService
    {
    public:
        DeckService(DeckStorage& storage, CollectionService& collections);

        // Hearthstone.Deck.MaxSaved, clamped to 1..kMaxSavedDecksLimit.
        void SetMaxSaved(uint32_t maxSaved);
        uint32_t MaxSaved() const;
        // Null: nothing is match-ready, CheckForMatch reports engine.
        void SetEngineSupport(EngineSupport predicate);
        bool HasEngineSupport() const;

        // Lifecycle like CollectionService: only accounts with a character in
        // the world keep a cache entry; SetOffline also evicts it.
        void SetOnline(uint32_t accountId);
        void SetOffline(uint32_t accountId);
        void Evict(uint32_t accountId);
        bool IsOnline(uint32_t accountId) const;
        std::size_t CachedCount() const;
        bool IsCached(uint32_t accountId) const;

        // Decks and unlocks (cached, loaded on demand). False: Unavailable.
        bool GetBook(uint32_t accountId, DeckBook& out);
        // The DECKS reply: decks with their state for a character of startClass
        // ("" = no character, locked is never reported). False: Unavailable.
        bool List(uint32_t accountId, std::string const& startClass, DeckList& out);

        // Creates (id 0) or updates a deck. startClass is the asking character's
        // starting class, for the state of the reply ("" = none).
        DeckSaveResult SaveDeck(uint32_t accountId, DeckSaveRequest const& req, std::string const& startClass);
        // Soft delete: the row stays (deleted = 1), the id is never reused.
        DeckDeleteResult DeleteDeck(uint32_t accountId, DeckDeleteRequest const& req);

        // Account-wide class access (quest unlocks and GM unlocks).
        ClassChangeResult UnlockClass(uint32_t accountId, std::string const& cls, std::string const& reason,
                                      std::string const& source);
        ClassChangeResult LockClass(uint32_t accountId, std::string const& cls, std::string const& reason,
                                    std::string const& source);

        // Records the starter award of a class and creates its deck when the list has room (see
        // StarterStatus). Does not grant cards and does not unlock the class.
        StarterAwardResult AwardStarter(uint32_t accountId, StarterAwardRequest const& req);

        // Revalidates rules, ownership and class access now, plus engine support.
        DeckMatchCheck CheckForMatch(uint32_t accountId, uint32_t deckId, std::string const& startClass);

    private:
        // Require m_mutex. Cached entry, or a fresh load (cached only when online).
        bool LoadLocked(uint32_t accountId, DeckBook& out);
        // Stores book as the account's cache entry when online, else drops it.
        void StoreLocked(uint32_t accountId, DeckBook const& book);
        ClassChangeResult ChangeClassLocked(uint32_t accountId, std::string const& cls, bool unlock,
                                            std::string const& reason, std::string const& source);

        DeckStorage& m_storage;
        CollectionService& m_collections;
        mutable std::mutex m_mutex;
        uint32_t m_maxSaved = kDefaultMaxSavedDecks;
        EngineSupport m_engine;
        std::unordered_set<uint32_t> m_online;
        std::unordered_map<uint32_t, DeckBook> m_cache;
    };
}

#endif
