/*
 * Opponents and their decks: the library of prepared decks
 * (hearthstone_deck_template), the opponent registry (creature entry -> per difficulty a pool of
 * library decks plus the encounter overrides of a boss) and the random pick of one deck of a pool.
 *
 * PURE C++ (standard library, the deck rules and the match engine header), tested headless in
 * tests/protocol_tests.cpp. The database glue (src/HsMatchStore.*) only reads the rows.
 *
 * Rules of a library deck (checked here, at startup, never at match start): exactly kDeckSize cards,
 * every card known to the CardDb and a minion, spell or weapon, the deck class one of the nine classes
 * and every card of that class or NEUTRAL, at most 2 copies of a card and 1 of a Legendary, and every
 * card supported by the match engine (CardDb::Supported). A deck that fails is not ready: it is kept
 * in the library with its first problem (for `.hs match decks`) but no opponent can use it.
 *
 * The registry is immutable after Build(); the glue swaps in a new one on a reload.
 */

#ifndef MOD_HEARTHSTONE_HS_OPPONENTS_H
#define MOD_HEARTHSTONE_HS_OPPONENTS_H

#include "HsEncounters.h"
#include "HsMatch.h"
#include "HsMatchAi.h"
#include "HsMatchService.h"

#include "../decks/HsDecks.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace Hs
{
    // ---- difficulty ---------------------------------------------------------------------------

    // Normal and Hard are the player's choice; Boss is fixed by the encounter (no choice, Hard AI).
    enum class Difficulty : uint8_t { Normal, Hard, Boss };
    char const* DifficultyCode(Difficulty d); // "normal", "hard", "boss" (the wire dif= and the ENUM)
    char const* DifficultyName(Difficulty d); // "Normal", "Hard", "Boss" (for players)
    // Exact lower case codes only.
    bool ParseDifficulty(std::string const& code, Difficulty& out);
    // Normal -> the Normal AI, Hard and Boss -> the Hard AI.
    Match::AiProfile AiProfileFor(Difficulty d);

    // ---- the deck library ------------------------------------------------------------------------

    constexpr std::size_t kMaxDeckKeyChars = 32;

    // Deck kinds (hearthstone_deck_template.kind) and match-finder pools (.finder).
    constexpr char const* kDeckKindOpponent = "opponent"; // the module decks normal_<class> / hard_<class>
    constexpr char const* kDeckKindStarter = "starter";   // starter_<class>: the class starter recipes
    constexpr char const* kDeckKindNpc = "npc";           // npc_<name>: the quest-unlocked NPC decks
    constexpr char const* kDeckKindLibrary = "library";   // lib_<class>_<entry>: the 2014 class library
    constexpr char const* kFinderNone = "none";
    // The key of a class's starter recipe: starter_<class in lower case>.
    std::string StarterDeckKey(std::string const& deckClass);

    // One hearthstone_deck_template row.
    struct LibraryDeckRow
    {
        std::string key;
        std::string name;
        std::string deckClass;
        std::string cards; // <cardId>:<count>,...
        std::string kind = kDeckKindOpponent;
        std::string finder = kFinderNone; // none, normal or hard
    };

    struct LibraryDeck
    {
        std::string key;
        std::string name;
        std::string deckClass;
        std::string kind = kDeckKindOpponent;
        std::string finder = kFinderNone;
        DeckCards cards;                 // parsed (empty when the text could not be parsed)
        std::vector<std::string> ids;    // one entry per copy, sorted by card id; only when ready
        bool ready = false;
        std::string problem;             // the first problem when !ready
    };

    // <cardId>:<count>,<cardId>:<count>,... Spaces around the parts are ignored. Rejects an empty
    // text, an empty entry, an id that is not [A-Za-z0-9_]{1,32}, a count that is not 1..3 decimal
    // digits, a count of 0 and a repeated id. error describes the first problem.
    bool ParseLibraryCards(std::string const& text, DeckCards& out, std::string& error);

    // "" when the deck follows the rules above, else the first problem (rules are checked in the order
    // class, size, known, type, class of the cards, copies, supported; each rule for every card before the
    // next one). requireSupported false skips the last rule (tests of the data).
    std::string CheckLibraryDeck(Match::CardDb const& db, std::string const& deckClass, DeckCards const& cards,
                                 bool requireSupported);

    class DeckLibrary
    {
    public:
        // Parses and checks every row. Decks are kept sorted by key. A row with a bad key (not
        // [0-9A-Za-z_]{1,32}) or a key listed twice is dropped. errors gets one line per deck that is not
        // ready or was dropped ("deck 'x': the reason"), for the log.
        static DeckLibrary Build(Match::CardDb const& db, std::vector<LibraryDeckRow> const& rows,
                                 std::vector<std::string>& errors);

        LibraryDeck const* Find(std::string const& key) const;
        std::vector<LibraryDeck> const& Decks() const { return m_decks; }
        std::size_t ReadyCount() const;

    private:
        std::vector<LibraryDeck> m_decks;
    };

    // ---- opponents --------------------------------------------------------------------------------

    // One hearthstone_opponent row. heroHealth, heroArmor, heroPower and heroCard are 0 / empty for NULL.
    struct OpponentRow
    {
        uint32_t entry = 0;
        std::string difficulty; // normal, hard or boss
        std::string deckKey;
        int heroHealth = 0;     // 0 = 30; otherwise 1..200
        int heroArmor = 0;      // 0..200
        std::string heroPower;  // a hero power card id; empty = the class hero power
        std::string heroCard;   // a hero card id; empty = the class hero
    };

    // The encounter advantages of one opponent; every field at its default = the normal rules.
    struct OpponentOverrides
    {
        int heroHealth = 0;
        int heroArmor = 0;
        std::string heroPowerId;
        std::string heroCardId;
    };

    // One hearthstone_npc_unlock row: a quest NPC that plays only for accounts that turned in quest_id.
    struct NpcUnlockRow
    {
        uint32_t entry = 0;
        uint32_t questId = 0;
        std::string deckKey;
        std::string faction; // alliance or horde
    };

    // What the registry hands out for one match.
    struct OpponentPick
    {
        Difficulty difficulty = Difficulty::Normal;
        Match::AiProfile profile = Match::AiProfile::Normal;
        std::string deckKey;
        std::string deckName;
        std::string deckClass;
        std::vector<std::string> deck; // one entry per copy
        OpponentOverrides overrides;
        // The boss encounter of the pick (HsEncounters.h), empty for every other opponent. deckKey is then the
        // encounter key as well; the start state, the first side and the human's start cards come from the encounter
        // (ApplyEncounter) and are not part of OpponentOverrides.
        std::string encounterKey;
    };

    class OpponentRegistry
    {
    public:
        // Builds the registry from the library, the database rows and the development config
        // (Hearthstone.Match.Opponents: entry + class -> normal_<class> / hard_<class>).
        //  - an entry with at least one database row is served by its rows only (rows win over the config);
        //  - a row whose deck is unknown or not ready, whose difficulty is bad or whose overrides are not
        //    valid (health 1..200, armor 0..200, hero card a Hero of the CardDb, hero power a supported
        //    HeroPower of the CardDb) is skipped;
        //  - an entry with a boss row is a boss entry: only its boss rows count;
        //  - a boss row whose deck_key is an encounter key (HsEncounters.h, "naxx_anubrekhan" ...) plays that encounter
        //    instead of a library deck: its hero_health / hero_power / hero_card / hero_armor must be NULL, and it is
        //    skipped with the first problem while the encounter is not ready (CheckEncounter with requireSupported);
        //  - an entry without any usable deck is dropped.
        // errors gets one line per skipped row, ignored config part or dropped entry.
        static OpponentRegistry Build(Match::CardDb const& db, DeckLibrary const& library,
                                      std::vector<OpponentRow> const& rows,
                                      std::vector<MatchOpponent> const& config, std::vector<std::string>& errors);
        // Plus the match-finder entries (hearthstone_finder). A finder entry offers Normal (every ready
        // library deck whose finder pool is normal) and Hard (pool hard); it is only a finder, so database rows or
        // a config entry for the same creature are ignored (with an error line). A finder without a ready deck in
        // either pool is dropped.
        static OpponentRegistry Build(Match::CardDb const& db, DeckLibrary const& library,
                                      std::vector<OpponentRow> const& rows,
                                      std::vector<MatchOpponent> const& config, std::vector<uint32_t> const& finders,
                                      std::vector<std::string>& errors);
        // Plus the quest NPCs (hearthstone_npc_unlock). Each offers Normal and Hard with its one ready deck
        // (owner decision: same deck, only the AI changes) and needs an account unlock (UnlockQuest != 0; the glue
        // checks it). A quest NPC is only that: rows, config or a finder row for the same creature are ignored (with
        // an error line). A row with a bad faction, quest 0 or a deck that is not ready is skipped.
        static OpponentRegistry Build(Match::CardDb const& db, DeckLibrary const& library,
                                      std::vector<OpponentRow> const& rows,
                                      std::vector<MatchOpponent> const& config, std::vector<uint32_t> const& finders,
                                      std::vector<NpcUnlockRow> const& unlocks, std::vector<std::string>& errors);
        // The same with the encounter table a boss row may name (the other overloads pass Encounters()). Tests
        // hand in their own table.
        static OpponentRegistry Build(Match::CardDb const& db, DeckLibrary const& library,
                                      std::vector<OpponentRow> const& rows,
                                      std::vector<MatchOpponent> const& config, std::vector<uint32_t> const& finders,
                                      std::vector<NpcUnlockRow> const& unlocks,
                                      std::vector<Encounter> const& encounters, std::vector<std::string>& errors);

        // The quest that unlocks a quest NPC; 0 for every other entry (no unlock needed, or unknown).
        uint32_t UnlockQuest(uint32_t entry) const;
        struct UnlockNpc
        {
            uint32_t entry = 0;
            uint32_t questId = 0;
            std::string faction;
        };
        // Every usable quest NPC, by entry.
        std::vector<UnlockNpc> UnlockNpcs() const;
        // The match-finder pick: uniform over the finder's pool for difficulty plus the pools of the given
        // quest NPCs (the caller passes the ones the account unlocked, of the player's side; entries that are not
        // quest NPCs are ignored). npcEntry is the chosen quest NPC, 0 for a library deck. False when finder is not a
        // finder or nothing is in the pool.
        bool PickFinder(uint32_t finder, Difficulty difficulty, std::vector<uint32_t> const& npcs, std::mt19937& rng,
                        OpponentPick& out, uint32_t& npcEntry) const;

        bool Has(uint32_t entry) const { return m_entries.count(entry) != 0; }
        std::size_t Count() const { return m_entries.size(); }
        std::vector<uint32_t> Entries() const; // ascending
        bool IsBoss(uint32_t entry) const;
        bool IsFinder(uint32_t entry) const;
        // The difficulties the player may choose from: Normal and / or Hard (those with at least one
        // ready deck), or just Boss for a boss entry. Empty for an unknown entry.
        std::vector<Difficulty> Offered(uint32_t entry) const;
        // How many decks the pool of entry and difficulty holds (0: none).
        std::size_t PoolSize(uint32_t entry, Difficulty difficulty) const;
        // Picks one deck of the pool uniformly with rng. False when entry has no pool for difficulty.
        bool Pick(uint32_t entry, Difficulty difficulty, std::mt19937& rng, OpponentPick& out) const;

    private:
        struct Choice
        {
            LibraryDeck deck;
            OpponentOverrides overrides;
            std::string encounter; // the encounter key, empty for a library deck
        };
        struct Entry
        {
            bool boss = false;
            bool finder = false;
            uint32_t unlockQuest = 0; // a quest NPC
            std::string faction;      // of a quest NPC
            std::map<Difficulty, std::vector<Choice>> pools;
        };
        static void Fill(OpponentPick& out, Difficulty difficulty, Choice const& choice);
        std::map<uint32_t, Entry> m_entries;
    };

    // ---- account unlocks of the quest NPCs -------------------------------------------

    class NpcUnlockStorage
    {
    public:
        virtual ~NpcUnlockStorage() = default;
        // The unlocked quest NPCs of an account: creature entry -> quest. False on a database failure.
        virtual bool Load(uint32_t accountId, std::map<uint32_t, uint32_t>& out) = 0;
        // Writes one unlock (idempotent: an existing row is kept). True only if it committed.
        virtual bool Commit(uint32_t accountId, uint32_t entry, uint32_t questId, std::string const& source) = 0;
    };

    // Per-account cache (online accounts only, like the collections) and the reconcile. Threading: every public
    // method takes the service mutex (a leaf lock apart from the storage, which takes none of the module's locks).
    class NpcUnlockService
    {
    public:
        explicit NpcUnlockService(NpcUnlockStorage& storage) : m_storage(storage) {}

        void SetOnline(uint32_t accountId);
        void SetOffline(uint32_t accountId); // also drops the cache entry
        // False: unavailable (the database could not be read).
        bool Get(uint32_t accountId, std::map<uint32_t, uint32_t>& out);
        // False also when unavailable.
        bool IsUnlocked(uint32_t accountId, uint32_t entry);
        // Unlocks every quest NPC of npcs whose quest is in completed and that the account does not have yet.
        // added gets the entries unlocked now (committed). False when the account could not be read or a commit
        // failed (the entries committed before stay in added; a retry finishes the rest).
        bool Reconcile(uint32_t accountId, std::vector<OpponentRegistry::UnlockNpc> const& npcs,
                       std::set<uint32_t> const& completed, std::string const& source, std::vector<uint32_t>& added);

    private:
        bool LoadLocked(uint32_t accountId, std::map<uint32_t, uint32_t>& out);

        NpcUnlockStorage& m_storage;
        std::mutex m_mutex;
        std::set<uint32_t> m_online;
        std::map<uint32_t, std::map<uint32_t, uint32_t>> m_cache;
    };

    // ---- the match-finder's opponent names -------------------------------------------

    // One usable hearthstone_finder_npc row: a real NPC whose name a random finder opponent takes.
    struct FinderName
    {
        uint32_t entry = 0;
        std::string name;    // creature_template.name
        std::string faction; // alliance, horde or any
    };

    // A random name for a character of the given faction (alliance or horde rows plus the any rows); "" when the
    // list holds none.
    std::string PickFinderName(std::vector<FinderName> const& names, bool alliance, std::mt19937& rng);
}

#endif
