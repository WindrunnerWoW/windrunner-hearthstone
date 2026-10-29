#include "HsArenaRules.h"

#include "../collection/HsTraderRules.h"

#include <algorithm>
#include <memory>
#include <random>
#include <utility>

namespace Hs
{
    namespace
    {
        template <typename T>
        T Clamp(T value, T lo, T hi)
        {
            return std::min(std::max(value, lo), hi);
        }

        // Rolls a rarity bucket by weight (all zero: the default weights).
        PackRarity RollArenaRarity(std::array<uint32_t, kPackRarityCount> const& weights, PackRng const& rng)
        {
            std::array<uint32_t, kPackRarityCount> w = weights;
            uint64_t total = 0;
            for (uint32_t x : w)
                total += x;
            if (total == 0)
                w = ArenaSettings().rarityWeights;
            uint32_t sum = 0;
            for (uint32_t x : w)
                sum += x;
            uint32_t r = rng(sum);
            for (std::size_t i = 0; i < kPackRarityCount; ++i)
            {
                if (r < w[i])
                    return static_cast<PackRarity>(i);
                r -= w[i];
            }
            return PackRarity::Common; // not reached: r < sum
        }

        // The cards of a bucket that are not in the offer yet.
        std::vector<std::string const*> Candidates(std::vector<std::string> const& bucket,
                                                   std::vector<std::string> const& taken)
        {
            std::vector<std::string const*> out;
            for (std::string const& id : bucket)
                if (std::find(taken.begin(), taken.end(), id) == taken.end())
                    out.push_back(&id);
            return out;
        }
    }

    char const* ArenaPayCode(ArenaPay pay)
    {
        switch (pay)
        {
            case ArenaPay::Free: return "free";
            case ArenaPay::Gold: return "gold";
            case ArenaPay::Coins: return "coins";
            case ArenaPay::Practice: return "practice";
        }
        return "free";
    }

    bool ParseArenaPay(std::string const& code, ArenaPay& out)
    {
        for (ArenaPay pay : { ArenaPay::Free, ArenaPay::Gold, ArenaPay::Coins, ArenaPay::Practice })
            if (code == ArenaPayCode(pay))
            {
                out = pay;
                return true;
            }
        return false;
    }

    ArenaSettings SanitizeArenaSettings(ArenaSettings s)
    {
        s.entryCostCopper = Clamp<uint32_t>(s.entryCostCopper, 1, kMaxArenaEntryCopper);
        s.entryCostCoins = Clamp<uint32_t>(s.entryCostCoins, 1, kMaxArenaEntryCoins);
        s.baseRewardPacks = Clamp<uint32_t>(s.baseRewardPacks, 1, kMaxArenaBaseRewardPacks);
        s.bonusCoins = std::min<uint32_t>(s.bonusCoins, kMaxArenaBonusCoins);
        uint64_t total = 0;
        for (uint32_t& w : s.rarityWeights)
        {
            w = std::min<uint32_t>(w, kMaxArenaRarityWeight);
            total += w;
        }
        if (total == 0)
            s.rarityWeights = ArenaSettings().rarityWeights;
        return s;
    }

    bool ParseArenaRarityWeights(std::string const& text, std::array<uint32_t, kPackRarityCount>& out)
    {
        std::array<uint32_t, kPackRarityCount> parsed {};
        std::size_t index = 0;
        std::size_t pos = 0;
        while (true)
        {
            std::size_t const end = text.find(',', pos);
            std::string part = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            while (!part.empty() && part.front() == ' ')
                part.erase(part.begin());
            while (!part.empty() && part.back() == ' ')
                part.pop_back();
            if (index >= kPackRarityCount || part.empty() || part.size() > 9)
                return false;
            uint32_t value = 0;
            for (char c : part)
            {
                if (c < '0' || c > '9')
                    return false;
                value = value * 10 + static_cast<uint32_t>(c - '0');
            }
            parsed[index++] = value;
            if (end == std::string::npos)
                break;
            pos = end + 1;
        }
        if (index != kPackRarityCount)
            return false;
        out = parsed;
        return true;
    }

    std::string FormatArenaRarityWeights(std::array<uint32_t, kPackRarityCount> const& weights)
    {
        std::string out;
        for (std::size_t i = 0; i < kPackRarityCount; ++i)
        {
            if (i)
                out += ',';
            out += std::to_string(weights[i]);
        }
        return out;
    }

    ArenaTerms MakeArenaTerms(ArenaSettings const& settings, ArenaPay pay, std::string const& datasetHash)
    {
        ArenaTerms terms;
        terms.pay = pay;
        terms.payAmount = pay == ArenaPay::Gold ? settings.entryCostCopper : pay == ArenaPay::Coins ? settings.entryCostCoins : 0;
        terms.basePacks = settings.baseRewardPacks;
        terms.bonusCoins = settings.bonusCoins;
        terms.rarityWeights = settings.rarityWeights;
        terms.rulesVersion = static_cast<uint32_t>(Match::kRulesVersion);
        terms.datasetHash = datasetHash;
        return terms;
    }

    ArenaPrize PrizeFor(uint32_t wins, ArenaTerms const& terms, bool specialSetsComplete)
    {
        ArenaPrize prize;
        if (wins == 0 || terms.pay == ArenaPay::Practice)
            return prize;
        if (wins < 3)
        {
            prize.packs = 1;
            return prize;
        }
        prize.packs = terms.basePacks;
        if (wins < 6)
            return prize;
        prize.coins = terms.bonusCoins;
        if (wins < kArenaMaxWins)
            return prize;
        if (specialSetsComplete)
            ++prize.packs;
        else
            prize.specialChoice = true;
        return prize;
    }

    std::vector<ArenaSpecialCard> const& ArenaSpecialCards()
    {
        static std::vector<ArenaSpecialCard> const cards = {
            { "EX1_062", 1 }, // Old Murk-Eye
            { "NEW1_016", 2 }, // Captain's Parrot
            { "EX1_112", 1 }, // Gelbin Mekkatorque
            { "PRO_001", 1 }  // Elite Tauren Chieftain
        };
        return cards;
    }

    ArenaSpecialCard const* FindArenaSpecialCard(std::string const& id)
    {
        for (ArenaSpecialCard const& card : ArenaSpecialCards())
            if (id == card.id)
                return &card;
        return nullptr;
    }

    uint32_t ArenaSpecialMissingCopies(ArenaSpecialCard const& card, Collection const& collection)
    {
        uint32_t const owned = collection.Get(card.id).Total();
        return owned >= card.copies ? 0 : card.copies - owned;
    }

    std::vector<ArenaSpecialMissing> ArenaMissingSpecials(Collection const& collection)
    {
        std::vector<ArenaSpecialMissing> out;
        for (ArenaSpecialCard const& card : ArenaSpecialCards())
        {
            uint32_t const missing = ArenaSpecialMissingCopies(card, collection);
            if (missing)
                out.push_back({ card.id, missing });
        }
        return out;
    }

    bool ArenaSpecialSetsComplete(Collection const& collection)
    {
        return ArenaMissingSpecials(collection).empty();
    }

    uint64_t ArenaPeriod(uint64_t now, uint32_t swapSeconds)
    {
        return TraderPeriod(now, swapSeconds);
    }

    bool ArenaCardOk(std::string const& id, CardRule const& rule, Match::CardDb const& db,
                     ArenaQuestSet const* questOwned)
    {
        if (!rule.collectible || rule.rarity == Rarity::None || !db.Supported(id))
            return false;
        if (rule.acquisition == Acquisition::Standard)
            return true;
        // A quest (Naxxramas) card only when the run's entry snapshot owns it.
        return rule.acquisition == Acquisition::Quest && questOwned && questOwned->count(id) != 0;
    }

    bool ArenaEligible(std::string const& id, CardRule const& rule, Match::CardDb const& db,
                       std::string const& deckClass, ArenaQuestSet const* questOwned)
    {
        return ArenaCardOk(id, rule, db, questOwned) && (rule.cardClass == deckClass || rule.cardClass == kNeutralClass);
    }

    std::vector<std::string> ArenaOwnedQuestCards(CardCatalog const& catalog, Collection const& collection)
    {
        std::vector<std::string> owned;
        catalog.ForEach([&](std::string const& id, CardRule const& rule)
        {
            if (rule.acquisition == Acquisition::Quest && collection.Get(id).Total() >= 1)
                owned.push_back(id);
        });
        std::sort(owned.begin(), owned.end());
        return owned;
    }

    ArenaPool ArenaPool::FromCatalog(CardCatalog const& catalog, Match::CardDb const& db, std::string const& deckClass,
                                     ArenaQuestSet const* questOwned)
    {
        ArenaPool pool;
        catalog.ForEach([&](std::string const& id, CardRule const& rule)
        {
            PackRarity bucket;
            if (!ArenaEligible(id, rule, db, deckClass, questOwned) || !PackRarityOf(rule.rarity, bucket))
                return;
            pool.m_cards[static_cast<std::size_t>(bucket)].push_back(id);
        });
        for (std::vector<std::string>& cards : pool.m_cards)
            std::sort(cards.begin(), cards.end());
        return pool;
    }

    std::size_t ArenaPool::Size() const
    {
        std::size_t n = 0;
        for (std::vector<std::string> const& cards : m_cards)
            n += cards.size();
        return n;
    }

    PackRng MakeSeededRng(uint64_t seed)
    {
        auto engine = std::make_shared<std::mt19937_64>(seed);
        return [engine](uint32_t bound) -> uint32_t
        {
            return bound <= 1 ? 0 : static_cast<uint32_t>((*engine)() % bound);
        };
    }

    bool ArenaOffer(ArenaPool const& pool, std::array<uint32_t, kPackRarityCount> const& weights, PackRng const& rng,
                    std::vector<std::string>& out)
    {
        if (pool.Size() < kArenaOfferSize)
            return false;
        std::vector<std::string> offer;
        while (offer.size() < kArenaOfferSize)
        {
            int const rolled = static_cast<int>(RollArenaRarity(weights, rng));
            // Next lower rarity when the rolled one is exhausted, and the higher ones only as the last resort.
            std::vector<std::string const*> candidates;
            for (int r = rolled; r >= 0 && candidates.empty(); --r)
                candidates = Candidates(pool.Cards(static_cast<PackRarity>(r)), offer);
            for (int r = rolled + 1; r < static_cast<int>(kPackRarityCount) && candidates.empty(); ++r)
                candidates = Candidates(pool.Cards(static_cast<PackRarity>(r)), offer);
            if (candidates.empty())
                return false; // not reached: the pool holds at least three cards
            offer.push_back(*candidates[rng(static_cast<uint32_t>(candidates.size()))]);
        }
        out = std::move(offer);
        return true;
    }

    std::vector<std::string> ArenaClassOffer(PackRng const& rng)
    {
        std::vector<std::string> classes = DeckClasses();
        std::vector<std::string> offer;
        while (offer.size() < kArenaClassOfferSize && !classes.empty())
        {
            std::size_t const i = rng(static_cast<uint32_t>(classes.size()));
            offer.push_back(classes[i]);
            classes.erase(classes.begin() + static_cast<std::ptrdiff_t>(i));
        }
        return offer;
    }

    char const* ArenaDeckWhyCode(ArenaDeckWhy why)
    {
        switch (why)
        {
            case ArenaDeckWhy::None: return "";
            case ArenaDeckWhy::Class: return "class";
            case ArenaDeckWhy::Size: return "size";
            case ArenaDeckWhy::Card: return "card";
            case ArenaDeckWhy::CardClass: return "cardclass";
        }
        return "";
    }

    char const* ArenaDeckWhyText(ArenaDeckWhy why)
    {
        switch (why)
        {
            case ArenaDeckWhy::None: return "ok";
            case ArenaDeckWhy::Class: return "not one of the nine classes";
            case ArenaDeckWhy::Size: return "a draft deck holds exactly 30 cards";
            case ArenaDeckWhy::Card: return "a card cannot be used in the arena";
            case ArenaDeckWhy::CardClass: return "a card belongs to another class";
        }
        return "";
    }

    ArenaDeckWhy ValidateArenaDeck(CardCatalog const& catalog, Match::CardDb const& db, std::string const& deckClass,
                                   std::vector<std::string> const& cards, ArenaQuestSet const* questOwned)
    {
        if (!IsDeckClass(deckClass))
            return ArenaDeckWhy::Class;
        if (cards.size() != kDeckSize)
            return ArenaDeckWhy::Size;
        for (std::string const& id : cards)
        {
            CardRule const* rule = catalog.Find(id);
            if (!rule || !ArenaCardOk(id, *rule, db, questOwned))
                return ArenaDeckWhy::Card;
        }
        for (std::string const& id : cards)
        {
            CardRule const* rule = catalog.Find(id);
            if (rule->cardClass != deckClass && rule->cardClass != kNeutralClass)
                return ArenaDeckWhy::CardClass;
        }
        return ArenaDeckWhy::None;
    }
}
