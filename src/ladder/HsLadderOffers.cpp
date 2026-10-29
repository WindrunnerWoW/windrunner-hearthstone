#include "HsLadderOffers.h"

#include <algorithm>
#include <string>

namespace Hs
{
namespace Ladder
{
    namespace
    {
        constexpr uint32_t kSeedSalt = 0x0FFE0001u;  // keeps the offer seeds apart from the round seeds
        constexpr uint32_t kPickSalt = 0x0FFE1000u;  // + the number of the pick
        constexpr int kRerollTries = 8;              // draws a reroll makes at most to get other offers

        // The places of the NPC in `npcs` (ladder order, best first) are positions 0..n-1; the player stands between
        // position p - 1 and position p. Position i is dist(i) places away: 1 for the NPC just above or just below.
        std::size_t Distance(std::size_t i, std::size_t p)
        {
            return i < p ? p - i : i - p + 1;
        }
    }

    uint32_t OfferSeed(uint32_t ladderSeed, uint32_t account, uint32_t drawCounter)
    {
        return Mix(Mix(ladderSeed ^ kSeedSalt, account), drawCounter);
    }

    std::vector<EntrantId> DrawOffers(LadderState const& state, EntrantId playerId, EntrantId lastOpponent, uint32_t seed,
                                      std::set<EntrantId> const& blocked, std::size_t count)
    {
        std::vector<EntrantId> out;
        if (count == 0)
            return out;

        // The enabled NPCs in ladder order.
        std::vector<Entrant const*> npcs;
        for (Entrant const* e : LadderOrder(state))
            if (!e->IsPlayer())
                npcs.push_back(e);
        std::size_t const n = npcs.size();
        if (n == 0)
            return out;

        // The player's place: a stored entry, or what a new entry would be.
        Entrant fresh;
        Entrant const* self = state.Find(playerId);
        if (!self)
        {
            fresh.id = playerId;
            fresh.rank = kBottomRank;
            fresh.stars = 0;
            fresh.rating = kStartRating;
            self = &fresh;
        }
        std::size_t p = 0;
        while (p < n && LadderLess(*npcs[p], *self))
            ++p;

        // Who may be offered. The last opponent only when nobody else is left.
        std::vector<char> ok(n, 0);
        std::size_t okCount = 0;
        for (std::size_t i = 0; i < n; ++i)
            if (!blocked.count(npcs[i]->id) && npcs[i]->id != lastOpponent)
            {
                ok[i] = 1;
                ++okCount;
            }
        if (okCount == 0)
            for (std::size_t i = 0; i < n; ++i)
                if (!blocked.count(npcs[i]->id))
                    ok[i] = 1;

        // The window, widened one place on each side until it holds enough candidates.
        std::vector<std::size_t> cand;
        for (std::size_t radius = kOfferWindow;; ++radius)
        {
            std::size_t const lo = p > radius ? p - radius : 0;
            std::size_t const hi = std::min(n, p + radius);
            cand.clear();
            for (std::size_t i = lo; i < hi; ++i)
                if (ok[i])
                    cand.push_back(i);
            if (cand.size() >= count || (lo == 0 && hi == n))
                break;
        }

        // The picks.
        std::set<std::string> classes;
        bool alliance = false, horde = false;
        std::vector<std::size_t> picks;
        for (std::size_t k = 0; k < count && !cand.empty(); ++k)
        {
            std::vector<int> score(cand.size(), 0);
            int best = 0;
            for (std::size_t c = 0; c < cand.size(); ++c)
            {
                Entrant const& e = *npcs[cand[c]];
                bool const newClass = classes.count(e.heroClass) == 0;
                bool const newFaction = (e.faction == Faction::Alliance && !alliance) || (e.faction == Faction::Horde && !horde);
                score[c] = (newClass ? 2 : 0) + (k > 0 && newFaction ? 1 : 0);
                best = std::max(best, score[c]);
            }
            std::size_t farthest = 0;
            for (std::size_t c = 0; c < cand.size(); ++c)
                if (score[c] == best)
                    farthest = std::max(farthest, Distance(cand[c], p));
            uint64_t total = 0;
            for (std::size_t c = 0; c < cand.size(); ++c)
                if (score[c] == best)
                    total += farthest - Distance(cand[c], p) + 1;
            uint64_t roll = Mix(seed, kPickSalt + static_cast<uint32_t>(k)) % total;
            std::size_t chosen = 0;
            for (std::size_t c = 0; c < cand.size(); ++c)
            {
                if (score[c] != best)
                    continue;
                uint64_t const weight = farthest - Distance(cand[c], p) + 1;
                if (roll < weight)
                {
                    chosen = c;
                    break;
                }
                roll -= weight;
            }
            Entrant const& e = *npcs[cand[chosen]];
            classes.insert(e.heroClass);
            alliance = alliance || e.faction == Faction::Alliance;
            horde = horde || e.faction == Faction::Horde;
            picks.push_back(cand[chosen]);
            cand.erase(cand.begin() + static_cast<std::ptrdiff_t>(chosen));
        }

        std::sort(picks.begin(), picks.end());
        for (std::size_t i : picks)
            out.push_back(npcs[i]->id);
        return out;
    }

    bool OfferSetValid(OfferSet const& set, LadderState const& state, std::set<EntrantId> const& blocked)
    {
        if (set.slots.empty())
            return false;
        std::set<EntrantId> seen;
        for (EntrantId id : set.slots)
        {
            Entrant const* e = state.Find(id);
            if (!e || e->IsPlayer() || !e->enabled || blocked.count(id) || !seen.insert(id).second)
                return false;
        }
        return true;
    }

    void RedrawOffers(OfferSet& set, LadderState const& state, uint32_t account, std::set<EntrantId> const& blocked,
                      std::size_t count)
    {
        set.account = account;
        std::vector<EntrantId> const previous = set.slots;
        for (int attempt = 0; attempt < kRerollTries; ++attempt)
        {
            ++set.drawCounter;
            set.slots = DrawOffers(state, PlayerId(account), set.lastOpponent,
                                   OfferSeed(state.seed, account, set.drawCounter), blocked, count);
            if (previous.empty() || set.slots != previous)
                break;
        }
        set.round = state.roundsPlayed;
    }

    bool EnsureOffers(OfferSet& set, LadderState const& state, uint32_t account, std::set<EntrantId> const& blocked,
                      std::size_t count)
    {
        if (set.account != account)
            set = OfferSet(); // the offers of another account are nobody's business here
        else if (OfferSetValid(set, state, blocked))
            return false;
        RedrawOffers(set, state, account, blocked, count);
        return true;
    }

    void OffersAfterMatch(OfferSet& set, EntrantId npc)
    {
        set.lastOpponent = npc;
        set.slots.clear();
    }
}
}
