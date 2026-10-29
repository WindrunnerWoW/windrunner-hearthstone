/*
 * Decks: the Hearthstone classes and the WoW class mapping, the deck struct,
 * the card-list codec, name validation, the hard deck rules, the draft states
 * and the DECKS / DECK_SAVED / DECK_DELETED / DECK_CHANGED payload encoders
 * (implementation contract, docs/PROTOCOL.md "Decks").
 *
 * PURE C++ (standard library only, no core headers) so it can be unit-tested
 * headlessly. The service lives in HsDeckService.*, the database glue in
 * src/HsDeckStore.*.
 */

#ifndef MOD_HEARTHSTONE_HS_DECKS_H
#define MOD_HEARTHSTONE_HS_DECKS_H

#include "../collection/HsOwnership.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace Hs
{
    // Cards of a complete deck, and the most a saved draft may hold.
    constexpr uint32_t kDeckSize = 30;
    // Deck name after trimming: 1..24 characters (code points), at most 64 bytes.
    constexpr std::size_t kMaxDeckNameChars = 24;
    constexpr std::size_t kMaxDeckNameBytes = 64;
    // hearthstone_deck.cards column size. Unreachable with the dataset's ids
    // (at most 8 characters: 30 entries need at most 329 bytes).
    constexpr std::size_t kMaxDeckCardsBytes = 1024;
    // Live decks an account may ever hold, even past the cap (saves with
    // bypassCap; starter awards respect the cap instead and grant only
    // the cards when the list is full). The bound keeps the worst-case DECKS reply within
    // kMaxPartsHardLimit frames: 29 decks of 30 distinct 8-character ids with
    // 64-byte non-ASCII names (24 characters), 10-digit ids and revisions need
    // 97 frames, 30 decks need more than 99 (tests/protocol_tests.cpp
    // TestDecksWorstCaseFits).
    constexpr uint32_t kMaxListedDecks = 29;
    // Hearthstone.Deck.MaxSaved: default and the largest value the config
    // accepts (20). The gap to kMaxListedDecks leaves
    // room for starter decks awarded past the cap.
    constexpr uint32_t kMaxSavedDecksLimit = 20;
    constexpr uint32_t kDefaultMaxSavedDecks = kMaxSavedDecksLimit;
    static_assert(kMaxSavedDecksLimit <= kMaxListedDecks, "the cap must fit the DECKS reply");
    // DECK_SAVE / DECK_DELETE key: [0-9a-z], 12..16 characters (like PACK_OPEN).
    constexpr std::size_t kMinDeckKeyLen = 12;
    constexpr std::size_t kMaxDeckKeyLen = 16;

    // Deck origins (hearthstone_deck.origin).
    constexpr char const* kDeckOriginPlayer = "player";
    constexpr char const* kDeckOriginGmFixture = "gm_fixture";
    constexpr char const* kDeckOriginStarter = "starter"; // a class starter deck award
    // Idempotency record ops (hearthstone_deck_op.op).
    constexpr char const* kDeckOpSave = "save";
    constexpr char const* kDeckOpDelete = "delete";

    // The nine deck classes in byte order: DRUID HUNTER MAGE PALADIN PRIEST ROGUE
    // SHAMAN WARLOCK WARRIOR. NEUTRAL cards fit every deck; it is no deck class.
    constexpr char const* kNeutralClass = "NEUTRAL";
    std::vector<std::string> const& DeckClasses();
    bool IsDeckClass(std::string const& cls); // exact, upper case
    // Case-insensitive lookup of a deck class name ("mage" -> "MAGE"); "" if none.
    std::string ParseDeckClass(std::string const& text);
    // The character's starting class: core Player::getClass() (1 WARRIOR,
    // 2 PALADIN, 3 HUNTER, 4 ROGUE, 5 PRIEST, 7 SHAMAN, 8 MAGE, 9 WARLOCK,
    // 11 DRUID) -> the Hearthstone class of the same name; "" for anything else.
    std::string DeckClassOfWowClass(uint32_t wowClass);

    // Card id -> copies, sorted by id in byte order.
    using DeckCards = std::map<std::string, uint32_t>;

    struct Deck
    {
        uint32_t id = 0;   // per account, never reused
        uint32_t rev = 0;  // 1 after the create, +1 per committed change
        std::string deckClass;
        std::string name;  // trimmed and validated
        DeckCards cards;
        std::string origin = kDeckOriginPlayer;
        // The stored card list could not be parsed (cards is empty then): the
        // deck is listed as invalid until it is saved again.
        bool damaged = false;

        uint32_t CardCount() const;
    };

    // <cardId>.<qty>,... sorted by id; "" for no cards.
    std::string EncodeDeckCards(DeckCards const& cards);
    // The inverse. Accepts any order; rejects an empty entry, an id that is not
    // [A-Za-z0-9_]{1,32}, a count that is not 1..3 decimal digits and duplicate
    // ids. A count of 0 or too many copies is a rule question, not a syntax one.
    bool DecodeDeckCards(std::string const& text, DeckCards& out);

    // Trims leading and trailing spaces, then requires 1..24 characters, at
    // most 64 bytes, valid UTF-8 (no overlong forms, no surrogates and nothing
    // above U+FFFF: the core's database connection is utf8mb3), no byte below
    // 0x20, no 0x7f and no pipe.
    bool NormalizeDeckName(std::string const& raw, std::string& out);

    // [0-9a-z]{12,16}
    bool IsValidDeckKey(std::string const& key);

    // Why a save or delete is refused (ERR REFUSED why=<code>). Nothing is written.
    enum class DeckWhy
    {
        None,
        Class,     // not one of the nine classes, or not the stored deck's class
        Name,      // NormalizeDeckName failed
        Card,      // unknown, not collectible, or acquisition not standard / reward_only / quest
        CardClass, // card class neither the deck class nor NEUTRAL
        Copies,    // a count of 0, more than 2, or more than 1 of a Legendary
        Size,      // more than 30 cards
        Cap,       // creating a deck while the account holds MaxSaved live decks
        Missing,   // update / delete of a deck the account does not have
        Conflict,  // rev is not the deck's current revision
        Key        // the key was already used for another operation or deck
    };
    char const* DeckWhyCode(DeckWhy why);  // "class", "name", ... ("" for None)
    char const* DeckWhyText(DeckWhy why);  // for players and GMs

    // The hard card rules of a deck of class deckClass (which must be a deck
    // class). Each rule is checked for every card before the next rule, so the
    // answer is the first of card, cardclass, copies, size that is broken.
    DeckWhy ValidateDeckCards(CardCatalog const& catalog, std::string const& deckClass, DeckCards const& cards);

    // Draft states of a saved deck, computed when needed, never stored.
    struct DeckState
    {
        bool size = false;    // fewer than 30 cards
        bool owned = false;   // the account owns fewer copies of a card than the deck uses
        bool invalid = false; // a hard rule is broken now (for example the card data changed)
        bool locked = false;  // the asking character cannot play the deck's class

        bool Complete() const { return !size && !owned && !invalid; }
        bool Playable() const { return Complete() && !locked; }
        // "ok" or the codes present in the order size,owned,invalid,locked.
        std::string Text() const;
    };

    // playable == nullptr: no character asks (GM view by account), locked is never set.
    DeckState EvaluateDeck(CardCatalog const& catalog, Collection const& collection, Deck const& deck,
                           std::set<std::string> const* playable);

    // Starting class (may be "") plus the account unlocks.
    std::set<std::string> PlayableClasses(std::string const& startClass, std::set<std::string> const& unlocks);

    // One deck of a DECKS reply with its state for the asking character.
    struct DeckListEntry
    {
        Deck deck;
        std::string state;
    };

    struct DeckList
    {
        uint32_t cap = 0;                  // Hearthstone.Deck.MaxSaved
        std::string start;                 // the asking character's starting class, "" if unknown
        std::vector<std::string> unlocked; // account unlocks, sorted
        std::vector<DeckListEntry> decks;  // live decks by id
    };

    // DECKS: cap=;n=;start=;unlocked=;d<i>=<id>.<rev>.<CLASS>.<origin>;m<i>=<name>;c<i>=<cards>;s<i>=<state>...
    std::string EncodeDecksPayload(DeckList const& list);
    // DECK_SAVED: key=;id=;rev=;replay=0|1;state=
    std::string EncodeDeckSavedPayload(std::string const& key, uint32_t id, uint32_t rev, bool replay,
                                       std::string const& state);
    // DECK_DELETED: key=;id=;replay=0|1
    std::string EncodeDeckDeletedPayload(std::string const& key, uint32_t id, bool replay);
    // DECK_CHANGED push: why=gm|class
    std::string EncodeDeckChangedPayload(std::string const& why);

    // Development fixture decks (used by the protocol tests): "mage" or
    // "warrior" (case-insensitive). Basic cards, 15 x 2 = 30. False for anything else.
    struct DeckFixture
    {
        std::string deckClass;
        std::string name;
        DeckCards cards;
    };
    bool GetDeckFixture(std::string const& which, DeckFixture& out);
}

#endif
