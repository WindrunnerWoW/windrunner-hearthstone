#include "HsOwnership.h"

#include "../protocol/HsProtocol.h"

#include <algorithm>
#include <utility>

namespace Hs
{
    namespace
    {
        std::string Upper(std::string s)
        {
            for (char& c : s)
                if (c >= 'a' && c <= 'z')
                    c = static_cast<char>(c - 'a' + 'A');
            return s;
        }

        bool IsValidCardId(std::string const& id)
        {
            if (id.empty() || id.size() > kMaxCardIdLen)
                return false;
            for (char c : id)
            {
                bool const ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
                if (!ok)
                    return false;
            }
            return true;
        }
    }

    bool ParseAcquisition(std::string const& s, Acquisition& out)
    {
        if (s == "standard")
            out = Acquisition::Standard;
        else if (s == "reward_only")
            out = Acquisition::RewardOnly;
        else if (s == "quest")
            out = Acquisition::Quest;
        else if (s == "none")
            out = Acquisition::None;
        else if (s == "unreleased")
            out = Acquisition::Unreleased;
        else
            return false;
        return true;
    }

    Rarity ParseRarity(std::string const& s)
    {
        if (s == "FREE")
            return Rarity::Free;
        if (s == "COMMON")
            return Rarity::Common;
        if (s == "RARE")
            return Rarity::Rare;
        if (s == "EPIC")
            return Rarity::Epic;
        if (s == "LEGENDARY")
            return Rarity::Legendary;
        return Rarity::None;
    }

    bool CardCatalog::Add(std::string const& id, bool collectible, std::string const& acquisition,
                          std::string const& rarity, std::string const& cardClass)
    {
        CardRule rule;
        if (!IsValidCardId(id) || !ParseAcquisition(acquisition, rule.acquisition) || m_rules.count(id))
            return false;
        rule.collectible = collectible;
        rule.rarity = ParseRarity(rarity);
        rule.legendary = rule.rarity == Rarity::Legendary;
        rule.cardClass = cardClass;
        m_rules.emplace(id, rule);

        std::string const key = Upper(id);
        auto const it = m_upper.find(key);
        if (it == m_upper.end())
            m_upper.emplace(key, id);
        else
            it->second.clear(); // two ids differ only by case: exact spelling required
        return true;
    }

    std::string CardCatalog::Resolve(std::string const& id) const
    {
        if (m_rules.count(id))
            return id;
        auto const it = m_upper.find(Upper(id));
        return it == m_upper.end() ? std::string() : it->second;
    }

    CardRule const* CardCatalog::Find(std::string const& canonicalId) const
    {
        auto const it = m_rules.find(canonicalId);
        return it == m_rules.end() ? nullptr : &it->second;
    }

    void CardCatalog::ForEach(std::function<void(std::string const& id, CardRule const& rule)> const& fn) const
    {
        for (auto const& entry : m_rules)
            fn(entry.first, entry.second);
    }

    CardCount Collection::Get(std::string const& id) const
    {
        auto const it = cards.find(id);
        return it == cards.end() ? CardCount() : it->second;
    }

    void Collection::Set(std::string const& id, CardCount count)
    {
        if (count.Total() == 0)
            cards.erase(id);
        else
            cards[id] = count;
    }

    Collection::Summary Collection::Summarize() const
    {
        Summary s;
        for (auto const& entry : cards)
        {
            ++s.distinct;
            s.copies += entry.second.Total();
            s.protectedCopies += entry.second.protectedQty;
        }
        return s;
    }

    bool ParseProtectionMode(std::string const& s, ProtectionMode& out)
    {
        std::string const u = Upper(s);
        if (u == "PROTECTED")
            out = ProtectionMode::Protected;
        else if (u == "UNPROTECTED")
            out = ProtectionMode::Unprotected;
        else if (u == "REWARD")
            out = ProtectionMode::Reward;
        else
            return false;
        return true;
    }

    char const* ProtectionModeName(ProtectionMode mode)
    {
        switch (mode)
        {
            case ProtectionMode::Protected: return "protected";
            case ProtectionMode::Unprotected: return "unprotected";
            case ProtectionMode::Reward: return "reward";
        }
        return "unprotected";
    }

    char const* OwnershipErrorText(OwnershipError e)
    {
        switch (e)
        {
            case OwnershipError::None: return "ok";
            case OwnershipError::UnknownCard: return "unknown card id";
            case OwnershipError::NotCollectible: return "card is not collectible";
            case OwnershipError::NotObtainable: return "card cannot be obtained (acquisition none or unreleased)";
            case OwnershipError::SourceNotAllowed: return "card cannot come from this source (reward-only: special rewards and GM; quest: quest rewards and GM)";
            case OwnershipError::BadCount: return "count must be 1..99";
            case OwnershipError::Overflow: return "an account may own at most 999 copies of a card";
            case OwnershipError::NotEnough: return "not enough copies owned";
        }
        return "error";
    }

    OwnershipError ValidateGrant(CardCatalog const& catalog, std::string const& id, GrantSource source,
                                 std::string& canonicalId, CardRule& rule)
    {
        std::string const resolved = catalog.Resolve(id);
        CardRule const* found = resolved.empty() ? nullptr : catalog.Find(resolved);
        if (!found)
            return OwnershipError::UnknownCard;
        if (!found->collectible)
            return OwnershipError::NotCollectible;
        if (found->acquisition == Acquisition::None || found->acquisition == Acquisition::Unreleased)
            return OwnershipError::NotObtainable;
        if (found->acquisition == Acquisition::RewardOnly &&
            source != GrantSource::SpecialReward && source != GrantSource::Gm)
            return OwnershipError::SourceNotAllowed;
        // Naxxramas cards come from quest rewards only (a GM may grant them for development). Never from a pack,
        // and not from the arena's special-card rewards, which are the promo cards' own rule.
        if (found->acquisition == Acquisition::Quest &&
            source != GrantSource::Quest && source != GrantSource::Gm)
            return OwnershipError::SourceNotAllowed;
        canonicalId = resolved;
        rule = *found;
        return OwnershipError::None;
    }

    OwnershipError PlanGrant(CardRule const& rule, CardCount const& current, uint32_t count, ProtectionMode mode,
                             CardCount& after)
    {
        if (count < 1 || count > kMaxCountPerOp)
            return OwnershipError::BadCount;
        if (current.Total() > kMaxCopiesPerCard || count > kMaxCopiesPerCard - current.Total())
            return OwnershipError::Overflow;

        uint32_t protectedAdd = 0;
        if (rule.acquisition == Acquisition::RewardOnly || mode == ProtectionMode::Protected)
            protectedAdd = count; // reward-only copies can never be scrapped
        else if (mode == ProtectionMode::Reward)
        {
            uint32_t const cap = rule.legendary ? kPlayableCopiesLegendary : kPlayableCopies;
            uint32_t const missing = current.Total() < cap ? cap - current.Total() : 0;
            protectedAdd = std::min(count, missing);
        }

        after = current;
        after.protectedQty += protectedAdd;
        after.unprotectedQty += count - protectedAdd;
        return OwnershipError::None;
    }

    OwnershipError PlanRemove(CardCount const& current, uint32_t count, bool all, CardCount& after)
    {
        if (all)
        {
            if (current.Total() == 0)
                return OwnershipError::NotEnough;
            after = CardCount();
            return OwnershipError::None;
        }
        if (count < 1 || count > kMaxCountPerOp)
            return OwnershipError::BadCount;
        if (count > current.Total())
            return OwnershipError::NotEnough;

        uint32_t const fromUnprotected = std::min(count, current.unprotectedQty);
        after.unprotectedQty = current.unprotectedQty - fromUnprotected;
        after.protectedQty = current.protectedQty - (count - fromUnprotected);
        return OwnershipError::None;
    }

    std::string EncodeCollPayload(Collection const& collection)
    {
        std::string cards;
        uint32_t n = 0;
        for (auto const& entry : collection.cards) // std::map: sorted by id
        {
            if (entry.second.Total() == 0)
                continue;
            if (n++)
                cards += ',';
            cards += entry.first + "." + std::to_string(entry.second.Total()) + "." +
                std::to_string(entry.second.protectedQty);
        }
        KvList kv;
        kv.push_back(std::make_pair("ver", std::to_string(collection.version)));
        kv.push_back(std::make_pair("n", std::to_string(n)));
        kv.push_back(std::make_pair("cards", cards));
        return KvEncode(kv);
    }

    std::string EncodeCollChangedPayload(uint32_t version)
    {
        KvList kv;
        kv.push_back(std::make_pair("ver", std::to_string(version)));
        return KvEncode(kv);
    }
}
