#include "HsPacks.h"

#include "../protocol/HsProtocol.h"

#include <algorithm>
#include <utility>

namespace Hs
{
    namespace
    {
        bool IsValidCardIdText(std::string const& id)
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

        std::string const& Pick(std::vector<std::string> const& cards, PackRng const& rng)
        {
            return cards[rng(static_cast<uint32_t>(cards.size()))];
        }

        PackRarity RollRarity(PackOdds const& odds, PackRng const& rng)
        {
            uint32_t total = 0;
            for (uint32_t w : odds.weight)
                total += w;
            uint32_t r = rng(total);
            for (std::size_t i = 0; i < kPackRarityCount; ++i)
            {
                if (r < odds.weight[i])
                    return static_cast<PackRarity>(i);
                r -= odds.weight[i];
            }
            return PackRarity::Common; // not reached: r < total
        }
    }

    char const* PackRarityName(PackRarity r)
    {
        switch (r)
        {
            case PackRarity::Common: return "Common";
            case PackRarity::Rare: return "Rare";
            case PackRarity::Epic: return "Epic";
            case PackRarity::Legendary: return "Legendary";
        }
        return "Common";
    }

    bool PackRarityOf(Rarity r, PackRarity& out)
    {
        switch (r)
        {
            case Rarity::Free:
            case Rarity::Common: out = PackRarity::Common; return true;
            case Rarity::Rare: out = PackRarity::Rare; return true;
            case Rarity::Epic: out = PackRarity::Epic; return true;
            case Rarity::Legendary: out = PackRarity::Legendary; return true;
            case Rarity::None: break;
        }
        return false;
    }

    PackOdds SanitizePackOdds(PackOdds odds)
    {
        uint32_t total = 0;
        for (uint32_t& w : odds.weight)
        {
            w = std::min<uint32_t>(w, 1000000);
            total += w;
        }
        if (total == 0)
            odds.weight = PackOdds().weight;
        odds.legendaryPity = std::min<uint32_t>(odds.legendaryPity, 1000);
        return odds;
    }

    PackPool PackPool::FromCatalog(CardCatalog const& catalog)
    {
        PackPool pool;
        catalog.ForEach([&pool](std::string const& id, CardRule const& rule)
        {
            PackRarity bucket;
            // Standard only: reward_only and quest (Naxxramas) cards never roll in a booster.
            if (rule.collectible && rule.acquisition == Acquisition::Standard && PackRarityOf(rule.rarity, bucket))
                pool.m_cards[static_cast<std::size_t>(bucket)].push_back(id);
        });
        for (auto& cards : pool.m_cards)
            std::sort(cards.begin(), cards.end());
        return pool;
    }

    bool PackPool::Complete() const
    {
        for (auto const& cards : m_cards)
            if (cards.empty())
                return false;
        return true;
    }

    std::size_t PackPool::Size() const
    {
        std::size_t n = 0;
        for (auto const& cards : m_cards)
            n += cards.size();
        return n;
    }

    bool PackRoll::HasLegendary() const
    {
        return std::find(rarities.begin(), rarities.end(), PackRarity::Legendary) != rarities.end();
    }

    bool RollPack(PackPool const& pool, PackOdds const& odds, uint32_t sinceLegendary, PackRng const& rng,
                  PackRoll& out)
    {
        if (!pool.Complete())
            return false;
        PackOdds const safe = SanitizePackOdds(odds);

        PackRoll roll;
        for (std::size_t i = 0; i < kPackSize; ++i)
        {
            roll.rarities[i] = RollRarity(safe, rng);
            roll.cards[i] = Pick(pool.Cards(roll.rarities[i]), rng);
        }

        // Bad-luck protection: this is the pity-th pack in a row without a Legendary.
        if (safe.legendaryPity > 0 && !roll.HasLegendary() && sinceLegendary + 1 >= safe.legendaryPity)
        {
            std::size_t lowest = 0;
            for (std::size_t i = 1; i < kPackSize; ++i)
                if (roll.rarities[i] < roll.rarities[lowest])
                    lowest = i;
            roll.rarities[lowest] = PackRarity::Legendary;
            roll.cards[lowest] = Pick(pool.Cards(PackRarity::Legendary), rng);
            roll.pityApplied = true;
        }

        // At least one Rare or better; Basic cards count as Common.
        bool allCommon = true;
        for (PackRarity r : roll.rarities)
            if (r != PackRarity::Common)
                allCommon = false;
        if (allCommon)
        {
            roll.rarities[kPackSize - 1] = PackRarity::Rare;
            roll.cards[kPackSize - 1] = Pick(pool.Cards(PackRarity::Rare), rng);
            roll.rareUpgraded = true;
        }

        out = std::move(roll);
        return true;
    }

    uint32_t SinceLegendaryAfter(PackRoll const& roll, uint32_t before)
    {
        if (roll.HasLegendary())
            return 0;
        return before == UINT32_MAX ? before : before + 1;
    }

    std::string PackOpening::NewMask() const
    {
        std::string mask;
        for (bool n : isNew)
            mask.push_back(n ? '1' : '0');
        return mask;
    }

    bool PackOpening::SetNewMask(std::string const& mask)
    {
        if (mask.size() != kPackSize)
            return false;
        std::array<bool, kPackSize> flags {};
        for (std::size_t i = 0; i < kPackSize; ++i)
        {
            if (mask[i] != '0' && mask[i] != '1')
                return false;
            flags[i] = mask[i] == '1';
        }
        isNew = flags;
        return true;
    }

    std::string PackOpening::CardList(char sep) const
    {
        std::string list;
        for (std::size_t i = 0; i < kPackSize; ++i)
        {
            if (i)
                list.push_back(sep);
            list += cards[i];
        }
        return list;
    }

    bool PackOpening::SetCardList(std::string const& list, char sep)
    {
        std::array<std::string, kPackSize> parsed;
        std::size_t n = 0;
        std::size_t start = 0;
        while (true)
        {
            std::size_t const end = list.find(sep, start);
            std::string const id = list.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (n >= kPackSize || !IsValidCardIdText(id))
                return false;
            parsed[n++] = id;
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
        if (n != kPackSize)
            return false;
        cards = parsed;
        return true;
    }

    bool IsValidPackKey(std::string const& key)
    {
        if (key.size() < kMinPackKeyLen || key.size() > kMaxPackKeyLen)
            return false;
        for (char c : key)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')))
                return false;
        return true;
    }

    std::array<bool, kPackSize> NewFlags(Collection const& before, std::array<std::string, kPackSize> const& cards)
    {
        std::array<bool, kPackSize> flags {};
        for (std::size_t i = 0; i < kPackSize; ++i)
        {
            bool const earlier = std::find(cards.begin(), cards.begin() + i, cards[i]) != cards.begin() + i;
            flags[i] = !earlier && before.Get(cards[i]).Total() == 0;
        }
        return flags;
    }

    std::string EncodePacksPayload(PackState const& state, std::vector<PackOpening> const& recent)
    {
        std::string list;
        std::size_t n = 0;
        for (PackOpening const& o : recent)
        {
            if (n == kRecentOpenings)
                break;
            if (n++)
                list.push_back(',');
            list += std::to_string(o.seq) + "." + o.NewMask() + "." + o.CardList('.');
        }
        KvList kv;
        kv.push_back(std::make_pair("type", std::string(kPackTypeClassic)));
        kv.push_back(std::make_pair("stock", std::to_string(state.stock)));
        kv.push_back(std::make_pair("opened", std::to_string(state.opened)));
        kv.push_back(std::make_pair("recent", list));
        return KvEncode(kv);
    }

    std::string EncodePackOpenedPayload(PackOpening const& opening, bool replay, uint32_t stockNow)
    {
        KvList kv;
        kv.push_back(std::make_pair("id", std::to_string(opening.seq)));
        kv.push_back(std::make_pair("key", opening.key));
        kv.push_back(std::make_pair("replay", std::string(replay ? "1" : "0")));
        kv.push_back(std::make_pair("stock", std::to_string(stockNow)));
        kv.push_back(std::make_pair("ver", std::to_string(opening.collectionVersion)));
        kv.push_back(std::make_pair("new", opening.NewMask()));
        kv.push_back(std::make_pair("cards", opening.CardList(',')));
        return KvEncode(kv);
    }

    std::string EncodePackChangedPayload(uint32_t stock)
    {
        KvList kv;
        kv.push_back(std::make_pair("stock", std::to_string(stock)));
        return KvEncode(kv);
    }

    PackSimStats SimulatePacks(PackPool const& pool, PackOdds const& odds, uint32_t packs, PackRng const& rng)
    {
        PackSimStats stats;
        uint32_t since = 0;
        for (uint32_t p = 0; p < packs; ++p)
        {
            PackRoll roll;
            if (!RollPack(pool, odds, since, rng, roll))
                break;
            ++stats.packs;
            bool rareOrBetter = false;
            bool allCards = true;
            for (std::size_t i = 0; i < kPackSize; ++i)
            {
                ++stats.cards[static_cast<std::size_t>(roll.rarities[i])];
                if (roll.rarities[i] != PackRarity::Common)
                    rareOrBetter = true;
                if (roll.cards[i].empty())
                    allCards = false;
            }
            if (!rareOrBetter || !allCards)
                ++stats.invalidPacks;
            if (roll.HasLegendary())
                ++stats.packsWithLegendary;
            if (roll.pityApplied)
                ++stats.pityApplied;
            if (roll.rareUpgraded)
                ++stats.rareUpgraded;
            since = SinceLegendaryAfter(roll, since);
            stats.longestWithoutLegendary = std::max(stats.longestWithoutLegendary, since);
        }
        return stats;
    }
}
