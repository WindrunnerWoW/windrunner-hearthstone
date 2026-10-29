/*
 * Battlegrounds minion pool: the shared stock of copies every shop draws from.
 *
 * PURE C++ (standard library, HsBgTypes.h).
 *
 *  - Copies per minion by tier 1..6: 15 / 15 / 13 / 11 / 9 / 7. Buying or being offered a minion takes a copy; selling
 *    it, refreshing the tavern, an unchosen Discover option and an eliminated player's board, hand and tavern put the
 *    copies back. The total of a card (pool + everything held) never changes: that is the invariant the tests check.
 *  - A draw is limited to a tier range (the shop's tavern tier) and picks a copy uniformly among the copies left, so a
 *    card with more copies left is proportionally more likely.
 *  - Lobby: 5 tribes are chosen by the seeded Rng from the tribes of the slice. Pool minions without a tribe and the
 *    "All" minion (Nightmare Amalgam) are always in. Only `slice15A` pool cards are used (tokens never are).
 *
 * POOL IDS. A pool is a numbered stock and a Draw looks into the list of pool ids it is given. The single-pool game builds
 * the shared pool 0 and every player draws from {0}. Faction-locked shops build one pool per faction
 * (0 = the allies, 1 = the enemy side), each holding its OWN copies of every card of the lobby's tribes, so a card can
 * live in more than one pool at once: the stock is keyed by (pool, card). Each player draws from the single pool of its
 * faction, `Return` names the pool the copy goes back to, and a minion changes faction hands only inside a faction
 * (sending), so the pool of the player who held it is always its home. Use AddCards / AddTribeCards.
 *
 * COPIES. A shared pool uses 15 / 15 / 13 / 11 / 9 / 7 per tier for a lobby of eight. A faction pool feeds four players,
 * so it halves them (8 / 8 / 7 / 6 / 5 / 4, rounded up) with `faction = true`, keeping a copy as scarce as in a live lobby.
 */

#ifndef MOD_HEARTHSTONE_HS_BG_POOL_H
#define MOD_HEARTHSTONE_HS_BG_POOL_H

#include "HsBgTypes.h"

#include <cstdint>
#include <vector>

namespace Hs
{
    namespace Bg
    {
        constexpr int kTribesPerLobby = 5;
        constexpr int kMaxPools = 16; // pool ids 0..kMaxPools-1 (uses 0 and 1)

        // Copies of each minion of a tier in the pool (tier 1..6; 0 for anything else).
        int CopiesForTier(int tier);
        // Copies of each minion of a tier in a faction pool: half of CopiesForTier, rounded up.
        int FactionCopiesForTier(int tier);
        // Fewer copies of each Demon in a faction pool (balance: a faction with Demons its opponent lacked
        // won 61% of bg_sim games; with 2 fewer copies about 55%). The cards themselves are untouched.
        constexpr int kFactionDemonCopiesLess = 2;
        // Copies of one card in a faction pool: FactionCopiesForTier, less kFactionDemonCopiesLess for a Demon (at
        // least 1).
        int FactionCopiesFor(int card);

        // The single-bit tribes (not kTribeAll) that appear on the pool minions of the slice, in bit order.
        std::vector<uint32_t> SliceTribes(bool slice15AOnly = true);
        // `count` of `available`, picked by shuffling with the Rng (the result is always drawn from the Rng, so the
        // game's random stream does not depend on how many tribes exist); all of them when there are not more than
        // `count`. Returned in bit order.
        std::vector<uint32_t> ChooseTribes(Rng& rng, std::vector<uint32_t> const& available, int count = kTribesPerLobby);

        // Is this card in a lobby with these tribes? A pool minion with no tribe or with kTribeAll is always in.
        bool CardInLobby(CardDef const& card, uint32_t tribeMask);

        class PoolSet
        {
        public:
            // Adds pool cards (card indexes) to pool `poolId`, each with the copies of its tier (a faction pool halves
            // them). A card already in THIS pool is ignored; the same card may live in another pool. Creates the
            // pool when it does not exist yet.
            void AddCards(int poolId, std::vector<int> const& cards, bool faction = false);
            // Adds every card of the lobby tribes (tribeless and "All" always) to `poolId`.
            void AddTribeCards(int poolId, uint32_t tribeMask, bool slice15AOnly = true, bool faction = false);
            // Lobby: chooses the tribes with the Rng and puts every slice card of those tribes, the tribeless ones and
            // the "All" ones into `poolId`. Returns the chosen tribes (bit order).
            std::vector<uint32_t> BuildLobby(Rng& rng, int poolId = 0, bool slice15AOnly = true, int tribeCount = kTribesPerLobby);

            int PoolCount() const { return m_pools; }
            bool Has(int card) const;                       // the card is in any pool of this lobby
            bool HasIn(int poolId, int card) const;         // the card is in pool `poolId`
            int Left(int card) const;                       // copies in all pools now
            int LeftIn(int poolId, int card) const;         // copies in pool `poolId` now
            int Initial(int card) const;                    // copies in all pools at the start
            int InitialIn(int poolId, int card) const;      // copies in pool `poolId` at the start
            int HomePool(int card) const;                   // the first pool holding the card, -1 when not in the lobby
            // The first pool of `poolIds` that holds the card, -1 when none does (the return-pool lookup).
            int HomeIn(int card, std::vector<int> const& poolIds) const;
            std::vector<int> const& Cards() const { return m_cardList; } // every distinct card of the lobby, table order
            int TotalLeft() const;

            // One random copy of a card of tiers [minTier, maxTier] with the tribes (0 = any; HasTribe semantics) out of
            // the pools; the copy is removed. -1 when nothing is left.
            // `excludeCard` (a card index) is never drawn.
            int Draw(Rng& rng, std::vector<int> const& poolIds, int minTier, int maxTier, uint32_t tribeAny = 0, int excludeCard = -1);
            // Up to `n` copies of DIFFERENT cards (a Discover). Fewer when fewer cards have copies left.
            std::vector<int> DrawDistinct(Rng& rng, std::vector<int> const& poolIds, int minTier, int maxTier,
                uint32_t tribeAny, int n);
            // Removes one copy of a specific card; false when none is left or the card is not in the lobby. The pool-less
            // forms use the card's home pool (HomePool); the faction shop always names the pool.
            bool Take(int card);
            bool Take(int card, int poolId);
            // Puts `n` copies back (never above the initial count). Cards outside the lobby are ignored.
            void Return(int card, int n = 1);
            void Return(int card, int poolId, int n);
            // Cards of the range that still have a copy left (candidates for a Draw).
            int CountCards(std::vector<int> const& poolIds, int minTier, int maxTier, uint32_t tribeAny) const;

        private:
            struct Entry
            {
                int card;
                int pool;
                int left;
                int initial;
            };
            bool Matches(Entry const& e, std::vector<int> const& poolIds, int minTier, int maxTier, uint32_t tribeAny,
                int excludeCard = -1) const;
            int EntryOf(int card, int poolId) const;   // -1 when absent

            std::vector<Entry> m_entries;   // sorted by (card, pool)
            std::vector<int> m_slot;        // card * kMaxPools + pool -> entry, -1 = not in the lobby
            std::vector<int> m_cardList;    // distinct cards, table order
            int m_pools = 0;
        };
    }
}

#endif
