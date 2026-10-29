#include "HsBgPool.h"

#include <algorithm>

namespace Hs
{
    namespace Bg
    {
        int CopiesForTier(int tier)
        {
            static int const copies[] = { 0, 15, 15, 13, 11, 9, 7 };
            return tier >= kMinTier && tier <= kMaxTier ? copies[tier] : 0;
        }

        int FactionCopiesForTier(int tier)
        {
            int const c = CopiesForTier(tier);
            return c == 0 ? 0 : (c + 1) / 2;
        }

        int FactionCopiesFor(int card)
        {
            CardDef const& d = CardAt(card);
            int const c = FactionCopiesForTier(d.tier);
            if (c == 0 || (d.tribes & kTribeDemon) == 0)   // a plain bit test: Amalgam's kTribeAll is not a Demon here
                return c;
            return std::max(1, c - kFactionDemonCopiesLess);
        }

        std::vector<uint32_t> SliceTribes(bool slice15AOnly)
        {
            uint32_t mask = 0;
            for (int i = 0; i < CardCount(); ++i)
            {
                CardDef const& c = CardAt(i);
                if (c.role != CardRole::Pool || (slice15AOnly && !c.slice15A))
                    continue;
                mask |= c.tribes & ~static_cast<uint32_t>(kTribeAll);
            }
            std::vector<uint32_t> out;
            for (uint32_t bit = 1; bit < kTribeAll; bit <<= 1)
                if (mask & bit)
                    out.push_back(bit);
            return out;
        }

        std::vector<uint32_t> ChooseTribes(Rng& rng, std::vector<uint32_t> const& available, int count)
        {
            std::vector<uint32_t> shuffled = available;
            rng.Shuffle(shuffled);
            if (static_cast<int>(shuffled.size()) > count)
                shuffled.resize(static_cast<std::size_t>(count));
            std::sort(shuffled.begin(), shuffled.end());
            return shuffled;
        }

        bool CardInLobby(CardDef const& card, uint32_t tribeMask)
        {
            if (card.role != CardRole::Pool)
                return false;
            return card.tribes == 0 || (card.tribes & kTribeAll) != 0 || (card.tribes & tribeMask) != 0;
        }

        void PoolSet::AddCards(int poolId, std::vector<int> const& cards, bool faction)
        {
            if (poolId < 0 || poolId >= kMaxPools)
                return;
            if (poolId + 1 > m_pools)
                m_pools = poolId + 1;
            if (m_slot.empty())
                m_slot.assign(static_cast<std::size_t>(CardCount()) * kMaxPools, -1);
            bool added = false;
            std::vector<char> seen(static_cast<std::size_t>(CardCount()), 0);
            for (int card : cards)
            {
                if (card < 0 || card >= CardCount() || EntryOf(card, poolId) >= 0 || seen[static_cast<std::size_t>(card)])
                    continue;
                seen[static_cast<std::size_t>(card)] = 1;
                int const copies = faction ? FactionCopiesFor(card) : CopiesForTier(CardAt(card).tier);
                if (copies <= 0)
                    continue;
                m_entries.push_back(Entry{ card, poolId, copies, copies });
                added = true;
            }
            if (!added)
                return;
            std::sort(m_entries.begin(), m_entries.end(), [](Entry const& a, Entry const& b)
            {
                return a.card != b.card ? a.card < b.card : a.pool < b.pool;
            });
            std::fill(m_slot.begin(), m_slot.end(), -1);
            m_cardList.clear();
            int lastCard = -1;
            for (int i = 0; i < static_cast<int>(m_entries.size()); ++i)
            {
                Entry const& e = m_entries[static_cast<std::size_t>(i)];
                m_slot[static_cast<std::size_t>(e.card) * kMaxPools + e.pool] = i;
                if (e.card != lastCard)
                {
                    m_cardList.push_back(e.card);
                    lastCard = e.card;
                }
            }
        }

        void PoolSet::AddTribeCards(int poolId, uint32_t tribeMask, bool slice15AOnly, bool faction)
        {
            std::vector<int> cards;
            for (int i = 0; i < CardCount(); ++i)
            {
                CardDef const& c = CardAt(i);
                if (slice15AOnly && !c.slice15A)
                    continue;
                if (CardInLobby(c, tribeMask))
                    cards.push_back(i);
            }
            AddCards(poolId, cards, faction);
        }

        std::vector<uint32_t> PoolSet::BuildLobby(Rng& rng, int poolId, bool slice15AOnly, int tribeCount)
        {
            std::vector<uint32_t> const tribes = ChooseTribes(rng, SliceTribes(slice15AOnly), tribeCount);
            uint32_t mask = 0;
            for (uint32_t t : tribes)
                mask |= t;
            AddTribeCards(poolId, mask, slice15AOnly, false);
            return tribes;
        }

        int PoolSet::EntryOf(int card, int poolId) const
        {
            if (card < 0 || card >= CardCount() || poolId < 0 || poolId >= kMaxPools)
                return -1;
            if (m_slot.empty())
                return -1;
            return m_slot[static_cast<std::size_t>(card) * kMaxPools + poolId];
        }

        bool PoolSet::Has(int card) const { return HomePool(card) >= 0; }
        bool PoolSet::HasIn(int poolId, int card) const { return EntryOf(card, poolId) >= 0; }

        int PoolSet::Left(int card) const
        {
            int n = 0;
            for (Entry const& e : m_entries)
                if (e.card == card)
                    n += e.left;
            return n;
        }

        int PoolSet::LeftIn(int poolId, int card) const
        {
            int const e = EntryOf(card, poolId);
            return e < 0 ? 0 : m_entries[static_cast<std::size_t>(e)].left;
        }

        int PoolSet::Initial(int card) const
        {
            int n = 0;
            for (Entry const& e : m_entries)
                if (e.card == card)
                    n += e.initial;
            return n;
        }

        int PoolSet::InitialIn(int poolId, int card) const
        {
            int const e = EntryOf(card, poolId);
            return e < 0 ? 0 : m_entries[static_cast<std::size_t>(e)].initial;
        }

        int PoolSet::HomePool(int card) const
        {
            for (Entry const& e : m_entries)
                if (e.card == card)
                    return e.pool;
            return -1;
        }

        int PoolSet::HomeIn(int card, std::vector<int> const& poolIds) const
        {
            for (Entry const& e : m_entries)
                if (e.card == card && std::find(poolIds.begin(), poolIds.end(), e.pool) != poolIds.end())
                    return e.pool;
            return -1;
        }

        int PoolSet::TotalLeft() const
        {
            int n = 0;
            for (Entry const& e : m_entries)
                n += e.left;
            return n;
        }

        bool PoolSet::Matches(Entry const& e, std::vector<int> const& poolIds, int minTier, int maxTier, uint32_t tribeAny,
            int excludeCard) const
        {
            if (e.left <= 0 || e.card == excludeCard)
                return false;
            if (std::find(poolIds.begin(), poolIds.end(), e.pool) == poolIds.end())
                return false;
            CardDef const& c = CardAt(e.card);
            if (c.tier < minTier || c.tier > maxTier)
                return false;
            return tribeAny == 0 || HasTribe(c.tribes, tribeAny);
        }

        int PoolSet::Draw(Rng& rng, std::vector<int> const& poolIds, int minTier, int maxTier, uint32_t tribeAny,
            int excludeCard)
        {
            uint32_t total = 0;
            for (Entry const& e : m_entries)
                if (Matches(e, poolIds, minTier, maxTier, tribeAny, excludeCard))
                    total += static_cast<uint32_t>(e.left);
            if (total == 0)
                return -1;
            uint32_t pick = rng.Uniform(total);
            for (Entry& e : m_entries)
            {
                if (!Matches(e, poolIds, minTier, maxTier, tribeAny, excludeCard))
                    continue;
                if (pick < static_cast<uint32_t>(e.left))
                {
                    --e.left;
                    return e.card;
                }
                pick -= static_cast<uint32_t>(e.left);
            }
            return -1; // unreachable
        }

        std::vector<int> PoolSet::DrawDistinct(Rng& rng, std::vector<int> const& poolIds, int minTier, int maxTier,
            uint32_t tribeAny, int n)
        {
            // Pick one copy at a time among the cards not chosen yet: a card with more copies left is likelier.
            std::vector<int> out;
            for (int k = 0; k < n; ++k)
            {
                uint32_t total = 0;
                for (Entry const& e : m_entries)
                    if (Matches(e, poolIds, minTier, maxTier, tribeAny) &&
                        std::find(out.begin(), out.end(), e.card) == out.end())
                        total += static_cast<uint32_t>(e.left);
                if (total == 0)
                    break;
                uint32_t pick = rng.Uniform(total);
                for (Entry& e : m_entries)
                {
                    if (!Matches(e, poolIds, minTier, maxTier, tribeAny) ||
                        std::find(out.begin(), out.end(), e.card) != out.end())
                        continue;
                    if (pick < static_cast<uint32_t>(e.left))
                    {
                        out.push_back(e.card);
                        break;
                    }
                    pick -= static_cast<uint32_t>(e.left);
                }
            }
            // The copies are removed only now, so the picks above all saw the same stock. A card drawn more than once
            // through different pools still takes one copy per occurrence.
            for (int card : out)
            {
                for (Entry& e : m_entries)
                    if (e.card == card && e.left > 0 &&
                        std::find(poolIds.begin(), poolIds.end(), e.pool) != poolIds.end())
                    {
                        --e.left;
                        break;
                    }
            }
            return out;
        }

        bool PoolSet::Take(int card) { return Take(card, HomePool(card)); }

        bool PoolSet::Take(int card, int poolId)
        {
            int const e = EntryOf(card, poolId);
            if (e < 0 || m_entries[static_cast<std::size_t>(e)].left <= 0)
                return false;
            --m_entries[static_cast<std::size_t>(e)].left;
            return true;
        }

        void PoolSet::Return(int card, int n) { Return(card, HomePool(card), n); }

        void PoolSet::Return(int card, int poolId, int n)
        {
            int const e = EntryOf(card, poolId);
            if (e < 0 || n <= 0)
                return;
            Entry& en = m_entries[static_cast<std::size_t>(e)];
            en.left = std::min(en.initial, en.left + n);
        }

        int PoolSet::CountCards(std::vector<int> const& poolIds, int minTier, int maxTier, uint32_t tribeAny) const
        {
            int n = 0;
            for (Entry const& e : m_entries)
                if (Matches(e, poolIds, minTier, maxTier, tribeAny))
                    ++n;
            return n;
        }
    }
}
