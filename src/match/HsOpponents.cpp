#include "HsOpponents.h"

#include "../collection/HsOwnership.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>

namespace Hs
{
    namespace
    {
        std::string Trim(std::string const& s)
        {
            std::size_t b = 0;
            std::size_t e = s.size();
            while (b < e && std::isspace(static_cast<unsigned char>(s[b])))
                ++b;
            while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
                --e;
            return s.substr(b, e - b);
        }

        bool IsIdChar(char c)
        {
            return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
        }

        bool IsCardId(std::string const& s)
        {
            if (s.empty() || s.size() > 32)
                return false;
            for (char c : s)
                if (!IsIdChar(c))
                    return false;
            return true;
        }

        bool IsDeckKey(std::string const& s)
        {
            if (s.empty() || s.size() > kMaxDeckKeyChars)
                return false;
            for (char c : s)
                if (!IsIdChar(c))
                    return false;
            return true;
        }

        std::string NameOf(Match::CardDb const& db, std::string const& id)
        {
            Match::CardDef const* def = db.Find(id);
            return def && !def->name.empty() ? def->name + " (" + id + ")" : id;
        }

        std::string Quote(std::string const& s)
        {
            return "'" + s + "'";
        }

        Encounter const* FindIn(std::vector<Encounter> const& encounters, std::string const& key)
        {
            for (Encounter const& e : encounters)
                if (e.key == key)
                    return &e;
            return nullptr;
        }
    }

    // ---- difficulty ----------------------------------------------------------------------------

    char const* DifficultyCode(Difficulty d)
    {
        switch (d)
        {
            case Difficulty::Normal:
                return "normal";
            case Difficulty::Hard:
                return "hard";
            case Difficulty::Boss:
                return "boss";
        }
        return "normal";
    }

    char const* DifficultyName(Difficulty d)
    {
        switch (d)
        {
            case Difficulty::Normal:
                return "Normal";
            case Difficulty::Hard:
                return "Hard";
            case Difficulty::Boss:
                return "Boss";
        }
        return "Normal";
    }

    bool ParseDifficulty(std::string const& code, Difficulty& out)
    {
        if (code == "normal")
            out = Difficulty::Normal;
        else if (code == "hard")
            out = Difficulty::Hard;
        else if (code == "boss")
            out = Difficulty::Boss;
        else
            return false;
        return true;
    }

    Match::AiProfile AiProfileFor(Difficulty d)
    {
        return d == Difficulty::Normal ? Match::AiProfile::Normal : Match::AiProfile::Hard;
    }

    // ---- library -------------------------------------------------------------------------------

    bool ParseLibraryCards(std::string const& text, DeckCards& out, std::string& error)
    {
        out.clear();
        error.clear();
        if (Trim(text).empty())
        {
            error = "the card list is empty";
            return false;
        }
        std::size_t start = 0;
        while (true)
        {
            std::size_t const comma = text.find(',', start);
            std::string const item = Trim(text.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
            if (item.empty())
            {
                error = "the card list has an empty entry";
                out.clear();
                return false;
            }
            std::size_t const colon = item.find(':');
            if (colon == std::string::npos)
            {
                error = "'" + item + "' is not <card id>:<count>";
                out.clear();
                return false;
            }
            std::string const id = Trim(item.substr(0, colon));
            std::string const count = Trim(item.substr(colon + 1));
            if (!IsCardId(id))
            {
                error = "'" + id + "' is not a card id";
                out.clear();
                return false;
            }
            if (count.empty() || count.size() > 3 || !std::all_of(count.begin(), count.end(), [](char c) { return c >= '0' && c <= '9'; }))
            {
                error = "the count of " + id + " is not a number of 1..3 digits";
                out.clear();
                return false;
            }
            uint32_t const n = static_cast<uint32_t>(std::stoul(count));
            if (n == 0)
            {
                error = "the count of " + id + " is 0";
                out.clear();
                return false;
            }
            if (!out.emplace(id, n).second)
            {
                error = id + " is listed twice";
                out.clear();
                return false;
            }
            if (comma == std::string::npos)
                return true;
            start = comma + 1;
        }
    }

    std::string CheckLibraryDeck(Match::CardDb const& db, std::string const& deckClass, DeckCards const& cards,
                                 bool requireSupported)
    {
        if (!IsDeckClass(deckClass))
            return "the class " + (deckClass.empty() ? std::string("(empty)") : Quote(deckClass)) +
                " is not one of the nine classes";
        uint64_t total = 0;
        for (auto const& entry : cards)
            total += entry.second;
        if (total != kDeckSize)
            return "the deck has " + std::to_string(total) + " cards instead of " + std::to_string(kDeckSize);

        for (auto const& entry : cards)
        {
            Match::CardDef const* def = db.Find(entry.first);
            if (!def)
                return "unknown card " + entry.first;
            if (def->type != Match::CardType::Minion && def->type != Match::CardType::Spell &&
                def->type != Match::CardType::Weapon)
                return NameOf(db, entry.first) + " cannot be in a deck";
        }
        for (auto const& entry : cards)
        {
            Match::CardDef const* def = db.Find(entry.first);
            if (def->cardClass != deckClass && def->cardClass != kNeutralClass)
                return NameOf(db, entry.first) + " belongs to " + (def->cardClass.empty() ? std::string("no class") : def->cardClass) +
                    ", not to " + deckClass;
        }
        for (auto const& entry : cards)
        {
            Match::CardDef const* def = db.Find(entry.first);
            uint32_t const limit = def->rarity == "LEGENDARY" ? kPlayableCopiesLegendary : kPlayableCopies;
            if (entry.second > limit)
                return std::to_string(entry.second) + " copies of " + NameOf(db, entry.first) + " (the limit is " +
                    std::to_string(limit) + ")";
        }
        if (requireSupported)
            for (auto const& entry : cards)
                if (!db.Supported(entry.first))
                    return NameOf(db, entry.first) + " is not supported by the match engine yet";
        return std::string();
    }

    DeckLibrary DeckLibrary::Build(Match::CardDb const& db, std::vector<LibraryDeckRow> const& rows,
                                   std::vector<std::string>& errors)
    {
        DeckLibrary library;
        std::set<std::string> seen;
        for (LibraryDeckRow const& row : rows)
        {
            if (!IsDeckKey(row.key))
            {
                errors.push_back("deck " + Quote(row.key) + ": the key must be 1.." + std::to_string(kMaxDeckKeyChars) +
                    " characters of [0-9A-Za-z_], the deck is dropped");
                continue;
            }
            if (!seen.insert(row.key).second)
            {
                errors.push_back("deck " + Quote(row.key) + ": the key is listed twice, the first deck is used");
                continue;
            }
            LibraryDeck deck;
            deck.key = row.key;
            deck.name = row.name.empty() ? row.key : row.name;
            deck.deckClass = row.deckClass;
            deck.kind = row.kind;
            deck.finder = row.finder;
            std::string problem;
            if (!ParseLibraryCards(row.cards, deck.cards, problem))
                deck.problem = "the card list cannot be read: " + problem;
            else
                deck.problem = CheckLibraryDeck(db, deck.deckClass, deck.cards, true);
            deck.ready = deck.problem.empty();
            if (deck.ready)
                ExpandDeck(deck.cards, deck.ids);
            else
                errors.push_back("deck " + Quote(deck.key) + " skipped: " + deck.problem);
            library.m_decks.push_back(std::move(deck));
        }
        std::sort(library.m_decks.begin(), library.m_decks.end(),
                  [](LibraryDeck const& a, LibraryDeck const& b) { return a.key < b.key; });
        return library;
    }

    LibraryDeck const* DeckLibrary::Find(std::string const& key) const
    {
        auto const it = std::lower_bound(m_decks.begin(), m_decks.end(), key,
                                         [](LibraryDeck const& d, std::string const& k) { return d.key < k; });
        return it != m_decks.end() && it->key == key ? &*it : nullptr;
    }

    std::size_t DeckLibrary::ReadyCount() const
    {
        std::size_t n = 0;
        for (LibraryDeck const& d : m_decks)
            if (d.ready)
                ++n;
        return n;
    }

    std::string StarterDeckKey(std::string const& deckClass)
    {
        std::string key = "starter_" + deckClass;
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return key;
    }

    // ---- registry ------------------------------------------------------------------------------

    OpponentRegistry OpponentRegistry::Build(Match::CardDb const& db, DeckLibrary const& library,
                                             std::vector<OpponentRow> const& rows,
                                             std::vector<MatchOpponent> const& config, std::vector<std::string>& errors)
    {
        return Build(db, library, rows, config, std::vector<uint32_t>(), errors);
    }

    OpponentRegistry OpponentRegistry::Build(Match::CardDb const& db, DeckLibrary const& library,
                                             std::vector<OpponentRow> const& rows,
                                             std::vector<MatchOpponent> const& config, std::vector<uint32_t> const& finders,
                                             std::vector<std::string>& errors)
    {
        return Build(db, library, rows, config, finders, std::vector<NpcUnlockRow>(), errors);
    }

    OpponentRegistry OpponentRegistry::Build(Match::CardDb const& db, DeckLibrary const& library,
                                             std::vector<OpponentRow> const& rows,
                                             std::vector<MatchOpponent> const& config, std::vector<uint32_t> const& finders,
                                             std::vector<NpcUnlockRow> const& unlocks, std::vector<std::string>& errors)
    {
        return Build(db, library, rows, config, finders, unlocks, Encounters(), errors);
    }

    OpponentRegistry OpponentRegistry::Build(Match::CardDb const& db, DeckLibrary const& library,
                                             std::vector<OpponentRow> const& rows,
                                             std::vector<MatchOpponent> const& config, std::vector<uint32_t> const& finders,
                                             std::vector<NpcUnlockRow> const& unlocks,
                                             std::vector<Encounter> const& encounters, std::vector<std::string>& errors)
    {
        OpponentRegistry registry;
        std::set<uint32_t> npcSet;
        for (NpcUnlockRow const& u : unlocks)
            npcSet.insert(u.entry);
        std::set<uint32_t> finderSet;
        for (uint32_t entry : finders)
        {
            if (npcSet.count(entry))
                errors.push_back("match-finder " + std::to_string(entry) + " ignored: the creature is a quest NPC");
            else
                finderSet.insert(entry);
        }

        // Database rows by entry (rows of an entry stay in the order given, which the store sorts).
        std::map<uint32_t, std::vector<OpponentRow const*>> byEntry;
        for (OpponentRow const& row : rows)
            byEntry[row.entry].push_back(&row);

        for (auto const& group : byEntry)
        {
            uint32_t const entry = group.first;
            std::string const who = "opponent " + std::to_string(entry);
            if (finderSet.count(entry) || npcSet.count(entry))
            {
                errors.push_back(who + ": its hearthstone_opponent rows are ignored, the creature is a " +
                    (npcSet.count(entry) ? "quest NPC" : "match-finder"));
                continue;
            }
            bool boss = false;
            for (OpponentRow const* row : group.second)
                if (row->difficulty == "boss")
                    boss = true;

            Entry result;
            result.boss = boss;
            for (OpponentRow const* row : group.second)
            {
                std::string const label = who + " row " + Quote(row->difficulty) + "/" + Quote(row->deckKey);
                Difficulty difficulty;
                if (!ParseDifficulty(row->difficulty, difficulty))
                {
                    errors.push_back(label + " skipped: the difficulty must be normal, hard or boss");
                    continue;
                }
                if (boss && difficulty != Difficulty::Boss)
                {
                    errors.push_back(label + " ignored: this entry has boss rows, which are its only decks");
                    continue;
                }
                // A boss row naming an encounter plays the encounter's deck, hero and start state.
                if (difficulty == Difficulty::Boss)
                    if (Encounter const* encounter = FindIn(encounters, row->deckKey))
                    {
                        if (row->heroHealth != 0 || row->heroArmor != 0 || !row->heroPower.empty() || !row->heroCard.empty())
                        {
                            errors.push_back(label + " skipped: an encounter row leaves hero_health, hero_armor, hero_power "
                                "and hero_card NULL (the encounter sets them)");
                            continue;
                        }
                        std::string const problem = CheckEncounter(db, *encounter, true);
                        if (!problem.empty())
                        {
                            errors.push_back(label + " skipped: the encounter is not ready (" + problem + ")");
                            continue;
                        }
                        Choice choice;
                        choice.encounter = encounter->key;
                        choice.deck.key = encounter->key;
                        choice.deck.name = encounter->name;
                        choice.deck.deckClass = encounter->heroClass;
                        choice.deck.kind = kDeckKindOpponent;
                        choice.deck.ids = ExpandEncounterDeck(*encounter);
                        choice.deck.ready = true;
                        choice.overrides.heroHealth = encounter->heroHealth;
                        choice.overrides.heroArmor = encounter->heroArmor;
                        choice.overrides.heroPowerId = encounter->heroPowerId;
                        choice.overrides.heroCardId = encounter->heroCardId;
                        result.pools[difficulty].push_back(std::move(choice));
                        continue;
                    }
                LibraryDeck const* deck = library.Find(row->deckKey);
                if (!deck)
                {
                    errors.push_back(label + " skipped: no library deck with this key");
                    continue;
                }
                if (!deck->ready)
                {
                    errors.push_back(label + " skipped: the deck is not ready (" + deck->problem + ")");
                    continue;
                }
                if (row->heroHealth < 0 || row->heroHealth > 200)
                {
                    errors.push_back(label + " skipped: hero_health must be 1..200");
                    continue;
                }
                if (row->heroArmor < 0 || row->heroArmor > 200)
                {
                    errors.push_back(label + " skipped: hero_armor must be 0..200");
                    continue;
                }
                if (!row->heroCard.empty())
                {
                    Match::CardDef const* hero = db.Find(row->heroCard);
                    if (!hero || hero->type != Match::CardType::Hero)
                    {
                        errors.push_back(label + " skipped: hero_card " + row->heroCard + " is not a hero card");
                        continue;
                    }
                }
                if (!row->heroPower.empty())
                {
                    Match::CardDef const* power = db.Find(row->heroPower);
                    if (!power || power->type != Match::CardType::HeroPower || !db.Supported(row->heroPower))
                    {
                        errors.push_back(label + " skipped: hero_power " + row->heroPower +
                            " is not a hero power the engine supports");
                        continue;
                    }
                }
                Choice choice;
                choice.deck = *deck;
                choice.overrides.heroHealth = row->heroHealth;
                choice.overrides.heroArmor = row->heroArmor;
                choice.overrides.heroPowerId = row->heroPower;
                choice.overrides.heroCardId = row->heroCard;
                result.pools[difficulty].push_back(std::move(choice));
            }
            if (result.pools.empty())
            {
                errors.push_back(who + " dropped: none of its rows is usable (the config does not replace them)");
                continue;
            }
            registry.m_entries[entry] = std::move(result);
        }

        // The development config: normal_<class> and hard_<class> of the library, for entries without rows.
        for (MatchOpponent const& opponent : config)
        {
            if (npcSet.count(opponent.entry))
            {
                errors.push_back("opponent " + std::to_string(opponent.entry) +
                    ": the config entry is ignored, the creature is a quest NPC");
                continue;
            }
            if (finderSet.count(opponent.entry))
            {
                errors.push_back("opponent " + std::to_string(opponent.entry) +
                    ": the config entry is ignored, the creature is a match-finder");
                continue;
            }
            if (byEntry.count(opponent.entry))
            {
                errors.push_back("opponent " + std::to_string(opponent.entry) +
                    ": the config entry is ignored, the database rows win");
                continue;
            }
            std::string cls = opponent.deckClass;
            std::transform(cls.begin(), cls.end(), cls.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            Entry result;
            for (Difficulty difficulty : { Difficulty::Normal, Difficulty::Hard })
            {
                std::string const key = std::string(DifficultyCode(difficulty)) + "_" + cls;
                LibraryDeck const* deck = library.Find(key);
                if (!deck)
                {
                    errors.push_back("opponent " + std::to_string(opponent.entry) + " (config): no library deck " + Quote(key));
                    continue;
                }
                if (!deck->ready)
                {
                    errors.push_back("opponent " + std::to_string(opponent.entry) + " (config): the deck " + Quote(key) +
                        " is not ready (" + deck->problem + ")");
                    continue;
                }
                Choice choice;
                choice.deck = *deck;
                result.pools[difficulty].push_back(std::move(choice));
            }
            if (result.pools.empty())
            {
                errors.push_back("opponent " + std::to_string(opponent.entry) + " (config) dropped: no ready deck of class " + cls);
                continue;
            }
            registry.m_entries[opponent.entry] = std::move(result);
        }

        // The match-finders share one pool per difficulty, built once.
        if (!finderSet.empty())
        {
            Entry finder;
            finder.finder = true;
            for (LibraryDeck const& deck : library.Decks())
            {
                Difficulty difficulty;
                if (!deck.ready || deck.finder == kFinderNone || !ParseDifficulty(deck.finder, difficulty) ||
                    difficulty == Difficulty::Boss)
                    continue;
                Choice choice;
                choice.deck = deck;
                finder.pools[difficulty].push_back(std::move(choice));
            }
            for (uint32_t entry : finderSet)
            {
                if (finder.pools.empty())
                {
                    errors.push_back("match-finder " + std::to_string(entry) +
                        " dropped: no ready library deck is in a finder pool");
                    continue;
                }
                registry.m_entries[entry] = finder;
            }
        }

        // The quest NPCs, Normal and Hard with the same deck.
        for (NpcUnlockRow const& u : unlocks)
        {
            std::string const who = "quest NPC " + std::to_string(u.entry);
            if (registry.m_entries.count(u.entry))
            {
                errors.push_back(who + ": listed twice, the first row is used");
                continue;
            }
            if (u.questId == 0 || (u.faction != "alliance" && u.faction != "horde"))
            {
                errors.push_back(who + " skipped: it needs a quest and the faction alliance or horde");
                continue;
            }
            LibraryDeck const* deck = library.Find(u.deckKey);
            if (!deck || !deck->ready)
            {
                errors.push_back(who + " skipped: the deck " + Quote(u.deckKey) +
                    (deck ? " is not ready (" + deck->problem + ")" : std::string(" is not in the library")));
                continue;
            }
            Entry npc;
            npc.unlockQuest = u.questId;
            npc.faction = u.faction;
            for (Difficulty difficulty : { Difficulty::Normal, Difficulty::Hard })
            {
                Choice choice;
                choice.deck = *deck;
                npc.pools[difficulty].push_back(std::move(choice));
            }
            registry.m_entries[u.entry] = std::move(npc);
        }
        return registry;
    }

    uint32_t OpponentRegistry::UnlockQuest(uint32_t entry) const
    {
        auto const it = m_entries.find(entry);
        return it == m_entries.end() ? 0 : it->second.unlockQuest;
    }

    std::vector<OpponentRegistry::UnlockNpc> OpponentRegistry::UnlockNpcs() const
    {
        std::vector<UnlockNpc> out;
        for (auto const& entry : m_entries)
            if (entry.second.unlockQuest != 0)
            {
                UnlockNpc n;
                n.entry = entry.first;
                n.questId = entry.second.unlockQuest;
                n.faction = entry.second.faction;
                out.push_back(n);
            }
        return out;
    }

    void OpponentRegistry::Fill(OpponentPick& out, Difficulty difficulty, Choice const& choice)
    {
        out = OpponentPick();
        out.difficulty = difficulty;
        out.profile = AiProfileFor(difficulty);
        out.deckKey = choice.deck.key;
        out.deckName = choice.deck.name;
        out.deckClass = choice.deck.deckClass;
        out.deck = choice.deck.ids;
        out.overrides = choice.overrides;
        out.encounterKey = choice.encounter;
    }

    bool OpponentRegistry::PickFinder(uint32_t finder, Difficulty difficulty, std::vector<uint32_t> const& npcs,
                                      std::mt19937& rng, OpponentPick& out, uint32_t& npcEntry) const
    {
        npcEntry = 0;
        auto const it = m_entries.find(finder);
        if (it == m_entries.end() || !it->second.finder)
            return false;
        // Every candidate once: the finder's own pool, then each distinct quest NPC's pool.
        std::vector<std::pair<uint32_t, Choice const*>> candidates;
        auto const pool = it->second.pools.find(difficulty);
        if (pool != it->second.pools.end())
            for (Choice const& c : pool->second)
                candidates.emplace_back(0u, &c);
        std::set<uint32_t> const distinct(npcs.begin(), npcs.end());
        for (uint32_t entry : distinct)
        {
            auto const npc = m_entries.find(entry);
            if (npc == m_entries.end() || npc->second.unlockQuest == 0)
                continue;
            auto const npcPool = npc->second.pools.find(difficulty);
            if (npcPool != npc->second.pools.end())
                for (Choice const& c : npcPool->second)
                    candidates.emplace_back(entry, &c);
        }
        if (candidates.empty())
            return false;
        auto const& chosen = candidates.size() == 1 ? candidates[0]
            : candidates[static_cast<std::size_t>(rng() % candidates.size())];
        Fill(out, difficulty, *chosen.second);
        npcEntry = chosen.first;
        return true;
    }

    // ---- account unlocks ---------------------------------------------------------------------------

    void NpcUnlockService::SetOnline(uint32_t accountId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_online.insert(accountId);
        m_cache.erase(accountId); // a stale entry from an earlier session
    }

    void NpcUnlockService::SetOffline(uint32_t accountId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_online.erase(accountId);
        m_cache.erase(accountId);
    }

    bool NpcUnlockService::LoadLocked(uint32_t accountId, std::map<uint32_t, uint32_t>& out)
    {
        auto const it = m_cache.find(accountId);
        if (it != m_cache.end())
        {
            out = it->second;
            return true;
        }
        std::map<uint32_t, uint32_t> loaded;
        if (!m_storage.Load(accountId, loaded))
            return false;
        if (m_online.count(accountId))
            m_cache[accountId] = loaded;
        out = std::move(loaded);
        return true;
    }

    bool NpcUnlockService::Get(uint32_t accountId, std::map<uint32_t, uint32_t>& out)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return LoadLocked(accountId, out);
    }

    bool NpcUnlockService::IsUnlocked(uint32_t accountId, uint32_t entry)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::map<uint32_t, uint32_t> unlocked;
        return LoadLocked(accountId, unlocked) && unlocked.count(entry) != 0;
    }

    bool NpcUnlockService::Reconcile(uint32_t accountId, std::vector<OpponentRegistry::UnlockNpc> const& npcs,
                                     std::set<uint32_t> const& completed, std::string const& source,
                                     std::vector<uint32_t>& added)
    {
        added.clear();
        std::lock_guard<std::mutex> lock(m_mutex);
        std::map<uint32_t, uint32_t> unlocked;
        if (!LoadLocked(accountId, unlocked))
            return false;
        bool ok = true;
        for (OpponentRegistry::UnlockNpc const& npc : npcs)
        {
            if (unlocked.count(npc.entry) || !completed.count(npc.questId))
                continue;
            if (!m_storage.Commit(accountId, npc.entry, npc.questId, source))
            {
                ok = false;
                continue;
            }
            unlocked[npc.entry] = npc.questId;
            added.push_back(npc.entry);
        }
        if (!ok)
            m_cache.erase(accountId); // the database state is unknown: read it again next time
        else if (m_online.count(accountId))
            m_cache[accountId] = unlocked;
        return ok;
    }

    bool OpponentRegistry::IsFinder(uint32_t entry) const
    {
        auto const it = m_entries.find(entry);
        return it != m_entries.end() && it->second.finder;
    }

    std::string PickFinderName(std::vector<FinderName> const& names, bool alliance, std::mt19937& rng)
    {
        std::vector<FinderName const*> eligible;
        for (FinderName const& n : names)
            if (!n.name.empty() && (n.faction == "any" || n.faction == (alliance ? "alliance" : "horde")))
                eligible.push_back(&n);
        if (eligible.empty())
            return std::string();
        return eligible[static_cast<std::size_t>(rng() % eligible.size())]->name;
    }

    std::vector<uint32_t> OpponentRegistry::Entries() const
    {
        std::vector<uint32_t> out;
        for (auto const& entry : m_entries)
            out.push_back(entry.first);
        return out;
    }

    bool OpponentRegistry::IsBoss(uint32_t entry) const
    {
        auto const it = m_entries.find(entry);
        return it != m_entries.end() && it->second.boss;
    }

    std::vector<Difficulty> OpponentRegistry::Offered(uint32_t entry) const
    {
        std::vector<Difficulty> out;
        auto const it = m_entries.find(entry);
        if (it == m_entries.end())
            return out;
        if (it->second.boss)
        {
            out.push_back(Difficulty::Boss);
            return out;
        }
        for (Difficulty d : { Difficulty::Normal, Difficulty::Hard })
        {
            auto const pool = it->second.pools.find(d);
            if (pool != it->second.pools.end() && !pool->second.empty())
                out.push_back(d);
        }
        return out;
    }

    std::size_t OpponentRegistry::PoolSize(uint32_t entry, Difficulty difficulty) const
    {
        auto const it = m_entries.find(entry);
        if (it == m_entries.end())
            return 0;
        auto const pool = it->second.pools.find(difficulty);
        return pool == it->second.pools.end() ? 0 : pool->second.size();
    }

    bool OpponentRegistry::Pick(uint32_t entry, Difficulty difficulty, std::mt19937& rng, OpponentPick& out) const
    {
        auto const it = m_entries.find(entry);
        if (it == m_entries.end())
            return false;
        auto const pool = it->second.pools.find(difficulty);
        if (pool == it->second.pools.end() || pool->second.empty())
            return false;
        std::vector<Choice> const& choices = pool->second;
        Choice const& choice = choices.size() == 1 ? choices[0] : choices[static_cast<std::size_t>(rng() % choices.size())];
        Fill(out, difficulty, choice);
        return true;
    }
}
