#include "HsDecks.h"

#include "../protocol/HsProtocol.h"

#include <utility>

namespace Hs
{
    namespace
    {
        bool IsCardIdChar(char c)
        {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        }

        std::string Upper(std::string s)
        {
            for (char& c : s)
                if (c >= 'a' && c <= 'z')
                    c = static_cast<char>(c - 'a' + 'A');
            return s;
        }

        bool IsContinuation(unsigned char c)
        {
            return c >= 0x80 && c <= 0xBF;
        }

        // Counts the code points of s. False on invalid UTF-8, a sequence
        // above U+FFFF, a byte below 0x20, 0x7f or a pipe.
        bool CheckNameBytes(std::string const& s, std::size_t& chars)
        {
            chars = 0;
            std::size_t i = 0;
            while (i < s.size())
            {
                unsigned char const c = static_cast<unsigned char>(s[i]);
                std::size_t len = 0;
                if (c < 0x80)
                {
                    if (c < 0x20 || c == 0x7f || c == '|')
                        return false;
                    len = 1;
                }
                else if (c >= 0xC2 && c <= 0xDF)
                    len = 2;
                else if (c >= 0xE0 && c <= 0xEF)
                    len = 3;
                else
                    return false; // continuation byte, overlong lead (C0, C1) or a 4-byte sequence (F0..)
                if (i + len > s.size())
                    return false;
                for (std::size_t k = 1; k < len; ++k)
                    if (!IsContinuation(static_cast<unsigned char>(s[i + k])))
                        return false;
                if (len == 3)
                {
                    unsigned char const c1 = static_cast<unsigned char>(s[i + 1]);
                    if (c == 0xE0 && c1 < 0xA0)
                        return false; // overlong
                    if (c == 0xED && c1 > 0x9F)
                        return false; // UTF-16 surrogate
                }
                i += len;
                ++chars;
            }
            return true;
        }

        DeckCards MakeCards(std::vector<char const*> const& ids, uint32_t copies)
        {
            DeckCards cards;
            for (char const* id : ids)
                cards[id] = copies;
            return cards;
        }
    }

    std::vector<std::string> const& DeckClasses()
    {
        static std::vector<std::string> const classes = {
            "DRUID", "HUNTER", "MAGE", "PALADIN", "PRIEST", "ROGUE", "SHAMAN", "WARLOCK", "WARRIOR"
        };
        return classes;
    }

    bool IsDeckClass(std::string const& cls)
    {
        for (std::string const& c : DeckClasses())
            if (c == cls)
                return true;
        return false;
    }

    std::string ParseDeckClass(std::string const& text)
    {
        std::string const upper = Upper(text);
        return IsDeckClass(upper) ? upper : std::string();
    }

    std::string DeckClassOfWowClass(uint32_t wowClass)
    {
        switch (wowClass)
        {
            case 1: return "WARRIOR";
            case 2: return "PALADIN";
            case 3: return "HUNTER";
            case 4: return "ROGUE";
            case 5: return "PRIEST";
            case 7: return "SHAMAN";
            case 8: return "MAGE";
            case 9: return "WARLOCK";
            case 11: return "DRUID";
            default: return std::string();
        }
    }

    uint32_t Deck::CardCount() const
    {
        uint32_t n = 0;
        for (auto const& entry : cards)
            n += entry.second;
        return n;
    }

    std::string EncodeDeckCards(DeckCards const& cards)
    {
        std::string out;
        for (auto const& entry : cards) // std::map: sorted by id (byte order)
        {
            if (!out.empty())
                out += ',';
            out += entry.first + "." + std::to_string(entry.second);
        }
        return out;
    }

    bool DecodeDeckCards(std::string const& text, DeckCards& out)
    {
        DeckCards cards;
        if (!text.empty())
        {
            std::size_t start = 0;
            while (true)
            {
                std::size_t const comma = text.find(',', start);
                std::string const entry = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                std::size_t const dot = entry.find('.');
                if (dot == std::string::npos || dot == 0 || dot > kMaxCardIdLen)
                    return false;
                std::string const id = entry.substr(0, dot);
                std::string const qty = entry.substr(dot + 1);
                for (char c : id)
                    if (!IsCardIdChar(c))
                        return false;
                if (qty.empty() || qty.size() > 3)
                    return false;
                uint32_t n = 0;
                for (char c : qty)
                {
                    if (c < '0' || c > '9')
                        return false;
                    n = n * 10 + static_cast<uint32_t>(c - '0');
                }
                if (!cards.emplace(id, n).second)
                    return false; // duplicate id
                if (comma == std::string::npos)
                    break;
                start = comma + 1;
            }
        }
        out = std::move(cards);
        return true;
    }

    bool NormalizeDeckName(std::string const& raw, std::string& out)
    {
        std::size_t first = 0;
        std::size_t last = raw.size();
        while (first < last && raw[first] == ' ')
            ++first;
        while (last > first && raw[last - 1] == ' ')
            --last;
        std::string const name = raw.substr(first, last - first);
        std::size_t chars = 0;
        if (name.empty() || name.size() > kMaxDeckNameBytes || !CheckNameBytes(name, chars) ||
            chars < 1 || chars > kMaxDeckNameChars)
            return false;
        out = name;
        return true;
    }

    bool IsValidDeckKey(std::string const& key)
    {
        if (key.size() < kMinDeckKeyLen || key.size() > kMaxDeckKeyLen)
            return false;
        for (char c : key)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')))
                return false;
        return true;
    }

    char const* DeckWhyCode(DeckWhy why)
    {
        switch (why)
        {
            case DeckWhy::None: return "";
            case DeckWhy::Class: return "class";
            case DeckWhy::Name: return "name";
            case DeckWhy::Card: return "card";
            case DeckWhy::CardClass: return "cardclass";
            case DeckWhy::Copies: return "copies";
            case DeckWhy::Size: return "size";
            case DeckWhy::Cap: return "cap";
            case DeckWhy::Missing: return "missing";
            case DeckWhy::Conflict: return "conflict";
            case DeckWhy::Key: return "key";
        }
        return "";
    }

    char const* DeckWhyText(DeckWhy why)
    {
        switch (why)
        {
            case DeckWhy::None: return "ok";
            case DeckWhy::Class: return "the deck class is not valid or differs from the saved deck's class";
            case DeckWhy::Name: return "a deck name needs 1 to 24 characters (no | or control characters)";
            case DeckWhy::Card: return "the deck contains a card that cannot be put into a deck";
            case DeckWhy::CardClass: return "the deck contains a card of another class";
            case DeckWhy::Copies: return "a deck holds 1 or 2 copies of a card, and only 1 of a Legendary";
            case DeckWhy::Size: return "a deck holds at most 30 cards";
            case DeckWhy::Cap: return "the account already has the maximum number of saved decks";
            case DeckWhy::Missing: return "the deck does not exist (any more)";
            case DeckWhy::Conflict: return "the deck was changed elsewhere in the meantime";
            case DeckWhy::Key: return "the request key was already used for another change";
        }
        return "error";
    }

    DeckWhy ValidateDeckCards(CardCatalog const& catalog, std::string const& deckClass, DeckCards const& cards)
    {
        for (auto const& entry : cards)
        {
            CardRule const* rule = catalog.Find(entry.first);
            // Standard, reward_only (promo) and quest (Naxxramas) cards may stand in a deck; whether the account owns
            // them is checked separately (EvaluateDeck "owned", and at match start).
            if (!rule || !rule->collectible ||
                (rule->acquisition != Acquisition::Standard && rule->acquisition != Acquisition::RewardOnly &&
                 rule->acquisition != Acquisition::Quest))
                return DeckWhy::Card;
        }
        for (auto const& entry : cards)
        {
            CardRule const* rule = catalog.Find(entry.first);
            if (rule->cardClass != deckClass && rule->cardClass != kNeutralClass)
                return DeckWhy::CardClass;
        }
        uint64_t total = 0;
        for (auto const& entry : cards)
        {
            CardRule const* rule = catalog.Find(entry.first);
            uint32_t const max = rule->legendary ? kPlayableCopiesLegendary : kPlayableCopies;
            if (entry.second == 0 || entry.second > max)
                return DeckWhy::Copies;
            total += entry.second;
        }
        if (total > kDeckSize || EncodeDeckCards(cards).size() > kMaxDeckCardsBytes)
            return DeckWhy::Size;
        return DeckWhy::None;
    }

    std::string DeckState::Text() const
    {
        std::string s;
        auto add = [&s](bool on, char const* code)
        {
            if (!on)
                return;
            if (!s.empty())
                s += ',';
            s += code;
        };
        add(size, "size");
        add(owned, "owned");
        add(invalid, "invalid");
        add(locked, "locked");
        return s.empty() ? std::string("ok") : s;
    }

    DeckState EvaluateDeck(CardCatalog const& catalog, Collection const& collection, Deck const& deck,
                           std::set<std::string> const* playable)
    {
        DeckState state;
        state.size = deck.CardCount() < kDeckSize;
        for (auto const& entry : deck.cards)
            if (collection.Get(entry.first).Total() < entry.second)
                state.owned = true;
        state.invalid = deck.damaged || !IsDeckClass(deck.deckClass) ||
            ValidateDeckCards(catalog, deck.deckClass, deck.cards) != DeckWhy::None;
        state.locked = playable && playable->count(deck.deckClass) == 0;
        return state;
    }

    std::set<std::string> PlayableClasses(std::string const& startClass, std::set<std::string> const& unlocks)
    {
        std::set<std::string> playable = unlocks;
        if (!startClass.empty())
            playable.insert(startClass);
        return playable;
    }

    std::string EncodeDecksPayload(DeckList const& list)
    {
        std::string unlocked;
        for (std::string const& c : list.unlocked)
        {
            if (!unlocked.empty())
                unlocked += ',';
            unlocked += c;
        }
        KvList kv;
        kv.push_back(std::make_pair("cap", std::to_string(list.cap)));
        kv.push_back(std::make_pair("n", std::to_string(list.decks.size())));
        kv.push_back(std::make_pair("start", list.start.empty() ? std::string("-") : list.start));
        kv.push_back(std::make_pair("unlocked", unlocked));
        std::size_t i = 0;
        for (DeckListEntry const& e : list.decks)
        {
            std::string const n = std::to_string(++i);
            kv.push_back(std::make_pair("d" + n, std::to_string(e.deck.id) + "." + std::to_string(e.deck.rev) + "." +
                e.deck.deckClass + "." + e.deck.origin));
            kv.push_back(std::make_pair("m" + n, e.deck.name));
            kv.push_back(std::make_pair("c" + n, EncodeDeckCards(e.deck.cards)));
            kv.push_back(std::make_pair("s" + n, e.state));
        }
        return KvEncode(kv);
    }

    std::string EncodeDeckSavedPayload(std::string const& key, uint32_t id, uint32_t rev, bool replay,
                                       std::string const& state)
    {
        KvList kv;
        kv.push_back(std::make_pair("key", key));
        kv.push_back(std::make_pair("id", std::to_string(id)));
        kv.push_back(std::make_pair("rev", std::to_string(rev)));
        kv.push_back(std::make_pair("replay", std::string(replay ? "1" : "0")));
        kv.push_back(std::make_pair("state", state));
        return KvEncode(kv);
    }

    std::string EncodeDeckDeletedPayload(std::string const& key, uint32_t id, bool replay)
    {
        KvList kv;
        kv.push_back(std::make_pair("key", key));
        kv.push_back(std::make_pair("id", std::to_string(id)));
        kv.push_back(std::make_pair("replay", std::string(replay ? "1" : "0")));
        return KvEncode(kv);
    }

    std::string EncodeDeckChangedPayload(std::string const& why)
    {
        KvList kv;
        kv.push_back(std::make_pair("why", why));
        return KvEncode(kv);
    }

    bool GetDeckFixture(std::string const& which, DeckFixture& out)
    {
        std::string const w = Upper(which);
        if (w == "MAGE")
        {
            out.deckClass = "MAGE";
            out.name = "Dev Mage";
            // Arcane Missiles, Murloc Raider, Arcane Explosion, Bloodfen Raptor,
            // Novice Engineer, River Crocolisk, Arcane Intellect, Raid Leader,
            // Wolfrider, Fireball, Oasis Snapjaw, Polymorph, Sen'jin Shieldmasta,
            // Nightblade, Boulderfist Ogre
            out.cards = MakeCards({ "EX1_277", "CS2_168", "CS2_025", "CS2_172", "EX1_015", "CS2_120", "CS2_023",
                "CS2_122", "CS2_124", "CS2_029", "CS2_119", "CS2_022", "CS2_179", "EX1_593", "CS2_200" }, 2);
            return true;
        }
        if (w == "WARRIOR")
        {
            out.deckClass = "WARRIOR";
            out.name = "Dev Warrior";
            // Execute, Whirlwind, Fiery War Axe, Heroic Strike, Cleave, Charge,
            // Shield Block, Warsong Commander, Kor'kron Elite, Arcanite Reaper,
            // Murloc Raider, Bloodfen Raptor, Wolfrider, Chillwind Yeti, Boulderfist Ogre
            out.cards = MakeCards({ "CS2_108", "EX1_400", "CS2_106", "CS2_105", "CS2_114", "CS2_103", "EX1_606",
                "EX1_084", "NEW1_011", "CS2_112", "CS2_168", "CS2_172", "CS2_124", "CS2_182", "CS2_200" }, 2);
            return true;
        }
        return false;
    }
}
