/*
 * Account-wide card ownership: card rules, quantities and the grant/remove
 * arithmetic (implementation contract).
 *
 * PURE C++ (standard library only, no core headers) so it can be unit-tested
 * headlessly. The database glue lives in src/HsCollectionStore.*.
 */

#ifndef MOD_HEARTHSTONE_HS_OWNERSHIP_H
#define MOD_HEARTHSTONE_HS_OWNERSHIP_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>

namespace Hs
{
    // Copies one grant or remove may move (GM "remove all" is the only exception).
    constexpr uint32_t kMaxCountPerOp = 99;
    // Copies of one card an account may own (protected + unprotected).
    constexpr uint32_t kMaxCopiesPerCard = 999;
    // Playable copies per deck; a reward fills protected copies up to this.
    constexpr uint32_t kPlayableCopies = 2;
    constexpr uint32_t kPlayableCopiesLegendary = 1;
    constexpr std::size_t kMaxCardIdLen = 32;

    // hearthstone_card_template.acquisition. What each value means for the rules ("Acquisition semantics"):
    //   Standard    Basic/Classic: packs, crafting, scrapping, the daily trader, arena offers, decks.
    //   RewardOnly  the four promo cards: special rewards only; decks; never packs/crafting/scrapping/trader/arena
    //               offers (their own arena milestone rules).
    //   Quest       collectible Naxxramas cards: earned only through quest rewards (and GM grants); decks once owned;
    //               never packs, crafting, scrapping, the daily trader or (for now) arena offers.
    //   None        runtime entities (heroes, hero powers, tokens, enchantments, encounter cards): never owned.
    //   Unreleased  flagged collectible in the build but never obtainable.
    enum class Acquisition { Standard, RewardOnly, Quest, None, Unreleased };
    bool ParseAcquisition(std::string const& s, Acquisition& out);

    // hearthstone_card_template.rarity (NULL or anything else is None).
    enum class Rarity { None, Free, Common, Rare, Epic, Legendary };
    Rarity ParseRarity(std::string const& s);

    struct CardRule
    {
        bool collectible = false;
        Acquisition acquisition = Acquisition::None;
        Rarity rarity = Rarity::None;
        bool legendary = false; // rarity == Legendary
        // hearthstone_card_template.card_class: MAGE, NEUTRAL, ... (decks).
        std::string cardClass;
    };

    // Card ids of the binary's dataset with the fields ownership needs. Filled
    // once at startup from the world database, read-only afterwards.
    class CardCatalog
    {
    public:
        // False (row skipped) for an id that is not [A-Za-z0-9_]{1,32}, an
        // unknown acquisition or a duplicate id. rarity is FREE, COMMON, RARE, EPIC or LEGENDARY (anything else: None).
        // cardClass is stored as given (DRUID ... WARRIOR, NEUTRAL, DREAM, ...).
        bool Add(std::string const& id, bool collectible, std::string const& acquisition, std::string const& rarity,
                 std::string const& cardClass);

        // Exact id first, then a case-insensitive match (unless two ids differ
        // only by case). Returns the canonical id, or an empty string.
        std::string Resolve(std::string const& id) const;
        CardRule const* Find(std::string const& canonicalId) const;
        std::size_t Size() const { return m_rules.size(); }
        // Every card, in no particular order.
        void ForEach(std::function<void(std::string const& id, CardRule const& rule)> const& fn) const;

    private:
        std::unordered_map<std::string, CardRule> m_rules;
        std::unordered_map<std::string, std::string> m_upper; // UPPER(id) -> id, "" when ambiguous
    };

    struct CardCount
    {
        uint32_t protectedQty = 0;
        uint32_t unprotectedQty = 0;
        uint32_t Total() const { return protectedQty + unprotectedQty; }
        bool operator==(CardCount const& o) const
        {
            return protectedQty == o.protectedQty && unprotectedQty == o.unprotectedQty;
        }
        bool operator!=(CardCount const& o) const { return !(*this == o); }
    };

    // One account's collection. cards never holds an entry with Total() == 0.
    struct Collection
    {
        uint32_t version = 0;
        std::map<std::string, CardCount> cards; // sorted by id (byte order)

        CardCount Get(std::string const& id) const;
        void Set(std::string const& id, CardCount count); // Total() == 0 erases

        struct Summary
        {
            uint32_t distinct = 0;
            uint32_t copies = 0;
            uint32_t protectedCopies = 0;
        };
        Summary Summarize() const;
    };

    // Where copies come from. Pack accepts only standard cards. Quest accepts standard and quest cards.
    // SpecialReward accepts standard and reward_only cards. Gm accepts standard, reward_only and quest cards.
    enum class GrantSource { Pack, Quest, SpecialReward, Gm };

    // How granted copies are stored. Reward fills the missing playable
    // copies (2, or 1 for a Legendary, counting all owned copies) as protected,
    // the rest unprotected. reward_only cards are always stored protected. Quest cards follow the mode (the quest
    // rewards use Reward: missing playable copies protected); quest cards can never be scrapped anyway (IsCraftable).
    enum class ProtectionMode { Protected, Unprotected, Reward };
    bool ParseProtectionMode(std::string const& s, ProtectionMode& out); // case-insensitive
    char const* ProtectionModeName(ProtectionMode mode);

    enum class OwnershipError
    {
        None,
        UnknownCard,
        NotCollectible,
        NotObtainable,  // acquisition none / unreleased
        SourceNotAllowed, // reward_only card from a pack or quest, quest card from a pack or special reward
        BadCount,       // outside 1..kMaxCountPerOp
        Overflow,       // more than kMaxCopiesPerCard afterwards
        NotEnough       // removing more than owned
    };
    char const* OwnershipErrorText(OwnershipError e);

    // Checks that id may be granted from source. On success canonicalId is the
    // dataset spelling and rule its entry.
    OwnershipError ValidateGrant(CardCatalog const& catalog, std::string const& id, GrantSource source,
                                 std::string& canonicalId, CardRule& rule);

    // New counts after granting count copies to current. Nothing is changed on error.
    OwnershipError PlanGrant(CardRule const& rule, CardCount const& current, uint32_t count, ProtectionMode mode,
                             CardCount& after);

    // New counts after removing count copies (unprotected ones first). all
    // removes every copy and ignores count.
    OwnershipError PlanRemove(CardCount const& current, uint32_t count, bool all, CardCount& after);

    // COLL reply payload: ver=<v>;n=<entries>;cards=<id>.<total>.<protected>,...
    std::string EncodeCollPayload(Collection const& collection);
    // COLL_CHANGED push payload: ver=<v>
    std::string EncodeCollChangedPayload(uint32_t version);
}

#endif
