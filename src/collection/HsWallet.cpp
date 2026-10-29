#include "HsWallet.h"

#include "../protocol/HsProtocol.h"

#include <algorithm>
#include <utility>

namespace Hs
{
    namespace
    {
        uint32_t ClampValue(uint32_t v, uint32_t lo, uint32_t hi)
        {
            return v < lo ? lo : (v > hi ? hi : v);
        }

        std::string JoinValues(std::array<uint32_t, 4> const& v)
        {
            return std::to_string(v[0]) + "." + std::to_string(v[1]) + "." + std::to_string(v[2]) + "." +
                std::to_string(v[3]);
        }
    }

    DustValues SanitizeDustValues(DustValues values)
    {
        for (std::size_t i = 0; i < 4; ++i)
        {
            values.scrap[i] = ClampValue(values.scrap[i], 1, 1000000);
            values.craft[i] = ClampValue(values.craft[i], 2, 1000000);
            // scrapping must give less than crafting costs, or craft-then-scrap makes dust out of nothing
            if (values.scrap[i] >= values.craft[i])
                values.scrap[i] = values.craft[i] - 1;
        }
        return values;
    }

    MatchRewards SanitizeMatchRewards(MatchRewards rewards)
    {
        rewards.loss = std::min<uint32_t>(rewards.loss, 1000);
        rewards.winNormal = std::min<uint32_t>(rewards.winNormal, 1000);
        rewards.winHard = std::min<uint32_t>(rewards.winHard, 1000);
        rewards.winBoss = std::min<uint32_t>(rewards.winBoss, 1000);
        return rewards;
    }

    uint32_t MatchCoins(MatchRewards const& rewards, std::string const& res, std::string const& why,
                        std::string const& difficulty)
    {
        if (res == "win" || res == "draw")
        {
            if (difficulty == "boss")
                return rewards.winBoss;
            if (difficulty == "hard")
                return rewards.winHard;
            return rewards.winNormal;
        }
        if (res == "loss" && why != "concede")
            return rewards.loss;
        return 0; // surrender, interrupted, anything unknown
    }

    bool DustBucket(Rarity rarity, std::size_t& out)
    {
        switch (rarity)
        {
            case Rarity::Free:
            case Rarity::Common: out = 0; return true;
            case Rarity::Rare: out = 1; return true;
            case Rarity::Epic: out = 2; return true;
            case Rarity::Legendary: out = 3; return true;
            case Rarity::None: break;
        }
        return false;
    }

    char const* DustBucketName(std::size_t bucket)
    {
        static char const* const kNames[] = { "COMMON", "RARE", "EPIC", "LEGENDARY" };
        return bucket < 4 ? kNames[bucket] : "COMMON";
    }

    bool IsCraftable(CardRule const& rule)
    {
        // Standard only: reward_only and quest cards can be neither crafted nor scrapped (see the header).
        std::size_t bucket = 0;
        return rule.collectible && rule.acquisition == Acquisition::Standard && DustBucket(rule.rarity, bucket);
    }

    uint32_t CraftLimit(CardRule const& rule)
    {
        return rule.legendary ? kPlayableCopiesLegendary : kPlayableCopies;
    }

    char const* CraftKindCode(CraftKind kind)
    {
        switch (kind)
        {
            case CraftKind::Make: return "craft";
            case CraftKind::Scrap: return "scrap";
            case CraftKind::Extras: return "extras";
        }
        return "craft";
    }

    char const* CraftWhyCode(CraftWhy why)
    {
        switch (why)
        {
            case CraftWhy::None: return "ok";
            case CraftWhy::Card: return "card";
            case CraftWhy::Dust: return "dust";
            case CraftWhy::Limit: return "limit";
            case CraftWhy::Copies: return "copies";
            case CraftWhy::Count: return "count";
            case CraftWhy::Npc: return "npc";
            case CraftWhy::Nothing: return "nothing";
        }
        return "card";
    }

    char const* CraftWhyText(CraftWhy why)
    {
        switch (why)
        {
            case CraftWhy::None: return "ok";
            case CraftWhy::Card: return "this card cannot be crafted or scrapped";
            case CraftWhy::Dust: return "you do not have enough dust";
            case CraftWhy::Limit: return "you already own every copy a deck can use";
            case CraftWhy::Copies: return "you have no copy of this card that can be scrapped (starter and quest copies are protected)";
            case CraftWhy::Count: return "you can scrap 1 to 99 copies at a time";
            case CraftWhy::Npc: return "talk to a Hearthstone Master to craft and scrap cards";
            case CraftWhy::Nothing: return "you have no extra cards: every card is within the copies a deck can use";
        }
        return "refused";
    }

    bool IsValidCraftKey(std::string const& key)
    {
        if (key.size() < kMinCraftKeyLen || key.size() > kMaxCraftKeyLen)
            return false;
        for (char c : key)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')))
                return false;
        return true;
    }

    std::string EncodeWalletPayload(WalletView const& view)
    {
        KvList kv;
        kv.push_back(std::make_pair("coins", std::to_string(view.coins)));
        kv.push_back(std::make_pair("dust", std::to_string(view.wallet.dust)));
        kv.push_back(std::make_pair("craft", std::string(view.craftOpen ? "1" : "0")));
        if (view.craftOpen && !view.craftNpc.empty())
            kv.push_back(std::make_pair("npc", view.craftNpc));
        kv.push_back(std::make_pair("scrap", JoinValues(view.values.scrap)));
        kv.push_back(std::make_pair("make", JoinValues(view.values.craft)));
        kv.push_back(std::make_pair("packcoins", std::to_string(view.packPriceCoins)));
        return KvEncode(kv);
    }

    std::string EncodeWalletChangedPayload(uint32_t coins, Wallet const& wallet, std::string const& why, uint32_t gain)
    {
        KvList kv;
        kv.push_back(std::make_pair("coins", std::to_string(coins)));
        kv.push_back(std::make_pair("dust", std::to_string(wallet.dust)));
        kv.push_back(std::make_pair("why", why));
        if (gain)
            kv.push_back(std::make_pair("gain", std::to_string(gain)));
        return KvEncode(kv);
    }

    std::string EncodeCraftedPayload(CraftRecord const& record, bool replay)
    {
        KvList kv;
        kv.push_back(std::make_pair("key", record.key));
        kv.push_back(std::make_pair("replay", std::string(replay ? "1" : "0")));
        kv.push_back(std::make_pair("kind", std::string(CraftKindCode(record.kind))));
        kv.push_back(std::make_pair("card", record.cardId));
        kv.push_back(std::make_pair("n", std::to_string(record.count)));
        kv.push_back(std::make_pair("delta", std::to_string(record.dustDelta)));
        kv.push_back(std::make_pair("dust", std::to_string(record.dustAfter)));
        kv.push_back(std::make_pair("ver", std::to_string(record.collectionVersion)));
        kv.push_back(std::make_pair("total", std::to_string(record.cardAfter.Total())));
        kv.push_back(std::make_pair("prot", std::to_string(record.cardAfter.protectedQty)));
        return KvEncode(kv);
    }

    std::string EncodeCraftAccessPayload(bool open, std::string const& npc)
    {
        KvList kv;
        kv.push_back(std::make_pair("open", std::string(open ? "1" : "0")));
        if (open && !npc.empty())
            kv.push_back(std::make_pair("npc", npc));
        return KvEncode(kv);
    }
}
