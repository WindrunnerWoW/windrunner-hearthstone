/*
 * Battlegrounds quest reward Shop behaviours: the reward effects that run inside the recruit
 * phase (the passive auras live on the RewardRule; the combat-side rewards are in HsBgCombat.cpp). Split from
 * HsBgRewards.cpp so a combat-only unit test can link the registry alone. See HsBgRewards.h.
 */

#include "HsBgRewards.h"
#include "HsBgShop.h"

#include <algorithm>
#include <numeric>
#include <utility>
#include <vector>

namespace Hs
{
    namespace Bg
    {
        namespace
        {
            std::vector<uint32_t> BoardUids(Board const& b)
            {
                std::vector<uint32_t> out;
                for (Minion const& m : b.Items())
                    out.push_back(m.uid);
                return out;
            }

            // A random subset of `n` distinct indexes of [0, size).
            std::vector<int> PickSome(Rng& rng, int size, int n)
            {
                std::vector<int> idx(static_cast<std::size_t>(std::max(0, size)));
                std::iota(idx.begin(), idx.end(), 0);
                rng.Shuffle(idx);
                if (static_cast<int>(idx.size()) > n)
                    idx.resize(static_cast<std::size_t>(std::max(0, n)));
                return idx;
            }

            int DistinctTribeCount(uint32_t mask)
            {
                int n = 0;
                for (int b = 0; b < 9; ++b)   // the nine real tribes (kTribeAll is not a type of its own here)
                    if (mask & (1u << b))
                        ++n;
                return n;
            }
        }

        AuraBonus Shop::RewardBoardAura(PlayerShop const& ps) const
        {
            AuraBonus a;
            if (RewardRule const* d = RewardFor(ps.rewardState))
                a.attack += d->boardAttack;
            if (RewardRule const* d = RewardFor(ps.rewardState2))
                a.attack += d->boardAttack;   // Denathrius can hold two rewards
            return a;
        }

        void Shop::CheckReward(int player)
        {
            if (!ValidPlayer(player))
                return;
            PlayerShop& ps = State(player);
            // Two quest slots (slot 0 standard, slot 1 Denathrius's second). Each completed quest
            // activates the reward in the matching slot.
            for (int slot = 0; slot < 2; ++slot)
            {
                QuestState& q = slot == 1 ? ps.quest2 : ps.quest;
                RewardState& r = slot == 1 ? ps.rewardState2 : ps.rewardState;
                if (r.active || !q.done || q.reward < 0 || q.reward >= RewardCount())
                    continue;
                r = RewardState{};
                r.id = q.reward;
                r.active = true;
                r.doneTurn = m_turn;   // for the simulation report
                RewardOnGain(player, slot);
                Settle(player);
            }
        }

        void Shop::RewardOnGain(int player, int slot)
        {
            PlayerShop& ps = State(player);
            QuestState& q = slot == 1 ? ps.quest2 : ps.quest;
            RewardState& r = slot == 1 ? ps.rewardState2 : ps.rewardState;
            RewardRule const* d = RewardFor(r);
            if (!d)
                return;
            switch (d->kind)
            {
            case RewardKind::GhastlyMask:
            {
                int const lf = FindCard("BGS_009");   // Lightfang Enforcer
                if (lf >= 0)
                    AddToHand(player, lf, false);
                break;
            }
            case RewardKind::Friends:
                // The type shown on the offer (Game draws it with the offer); a reward gained without one draws it now.
                if (q.rewardType != 0)
                    r.friendType = q.rewardType;
                else
                {
                    std::vector<uint32_t> const& tribes = PlayerLobbyTribes(player);
                    if (!tribes.empty())
                        r.friendType = tribes[m_rng.Uniform(static_cast<uint32_t>(tribes.size()))];
                }
                break;
            default:
                break;
            }
        }

        void Shop::RewardStartTurn(int player)
        {
            PlayerShop& ps = State(player);
            for (int slot = 0; slot < 2; ++slot)
            {
                RewardState& r = slot == 1 ? ps.rewardState2 : ps.rewardState;
                RewardRule const* d = RewardFor(r);
                if (!d)
                    continue;
                switch (d->kind)
                {
                case RewardKind::RedHand:
                    if (!ps.hand.Empty())
                        Buff(player, ps.hand[m_rng.Range(0, ps.hand.Size() - 1)], 12, 12);
                    break;
                case RewardKind::Friends:
                    if (r.friendType != 0)
                        for (int k = 0; k < 2; ++k)
                            AddRandomToHand(player, kMinTier, kMaxTier, r.friendType);
                    break;
                case RewardKind::Yogg:
                    Wheel(player);
                    break;
                case RewardKind::AlterEgo:
                    r.alterEgoOdd = !r.alterEgoOdd;   // the Even/Odd swap
                    break;
                default:
                    break;
                }
            }
        }

        void Shop::RewardEndTurn(int player)
        {
            PlayerShop& ps = State(player);
            for (int slot = 0; slot < 2; ++slot)
            {
                RewardState& r = slot == 1 ? ps.rewardState2 : ps.rewardState;
                RewardRule const* d = RewardFor(r);
                if (!d)
                    continue;
                switch (d->kind)
                {
            case RewardKind::SnickerSnacks:
            {
                std::vector<int> withBattlecry;   // only minions that have a Battlecry can be picked
                for (int i = 0; i < ps.board.Size(); ++i)
                    if (EffectDef const* e = m_effects.Find(ps.board[i].card); e && e->battlecry)
                        withBattlecry.push_back(i);
                m_rng.Shuffle(withBattlecry);
                if (withBattlecry.size() > 2)
                    withBattlecry.resize(2);
                std::vector<uint32_t> uids;   // by uid, because a Battlecry may change the board
                for (int i : withBattlecry)
                    uids.push_back(ps.board[i].uid);
                for (uint32_t uid : uids)
                    if (ps.board.IndexOfUid(uid) >= 0)
                        TriggerBattlecry(player, uid, 0);
                break;
            }
            case RewardKind::Parasol:
                if (!ps.board.Empty())
                {
                    Minion& m = ps.board[ps.board.Size() - 1];
                    GiveKeywords(player, m, kKwStealth);
                    Buff(player, m, 0, 8);
                }
                break;
            case RewardKind::TinyHenchmen:
            {
                std::vector<int> small;
                for (int i = 0; i < ps.board.Size(); ++i)
                    if (ps.board[i].Tier() <= 3)
                        small.push_back(i);
                m_rng.Shuffle(small);
                if (static_cast<int>(small.size()) > 3)
                    small.resize(3);
                std::vector<uint32_t> uids;
                for (int i : small)
                    uids.push_back(ps.board[i].uid);
                for (uint32_t uid : uids)
                {
                    int const i = ps.board.IndexOfUid(uid);
                    if (i >= 0)
                        Buff(player, ps.board[i], 2, 2);
                }
                break;
            }
            case RewardKind::DevilsInTheDetails:
            {
                // The left- and right-most minions each consume a random tavern minion.
                if (ps.board.Empty())
                    break;
                int const left = static_cast<int>(ps.board[0].uid);
                int const right = static_cast<int>(ps.board[ps.board.Size() - 1].uid);
                for (int uid : { left, right })
                {
                    int const idx = ps.board.IndexOfUid(static_cast<uint32_t>(uid));
                    if (ps.offers.empty() || idx < 0)
                        continue;
                    Minion eaten;
                    int const pick = m_rng.Range(0, static_cast<int>(ps.offers.size()) - 1);
                    if (ConsumeOffer(player, pick, eaten))
                        Buff(player, ps.board[idx], eaten.attack, eaten.health);
                }
                break;
            }
            case RewardKind::MenagerieMayhem:
            {
                uint32_t mask = 0;
                for (Minion const& m : ps.board.Items())
                    mask |= m.Tribes();
                int const types = DistinctTribeCount(mask);
                if (types > 0)
                    for (uint32_t uid : BoardUids(ps.board))   // by uid: a buff hook may change the board
                        if (Minion* m = FindMinion(player, uid))
                            Buff(player, *m, types, 0);
                break;
            }
            default:
                break;
            }
            }
        }

        void Shop::RewardRefresh(int player)
        {
            PlayerShop& ps = State(player);
            for (int slot = 0; slot < 2; ++slot)
            {
                RewardState& r = slot == 1 ? ps.rewardState2 : ps.rewardState;
                RewardRule const* d = RewardFor(r);
                if (!d)
                    continue;
                if (d->kind == RewardKind::MirrorShield)
                {
                    if (!ps.offers.empty())
                    {
                        Minion& o = ps.offers[m_rng.Range(0, static_cast<int>(ps.offers.size()) - 1)];
                        Buff(player, o, 4, 4);
                        GiveKeywords(player, o, kKwDivineShield);
                    }
                }
                else if (d->kind == RewardKind::Wisdomball)
                {
                    RewardWisdomball(player);
                }
            }
        }

        // Wondrous Wisdomball: 1-in-3 refreshes is "helpful" and re-draws the offers
        // preferring a minion that completes a triple the owner holds, else a tribe already on the owner's board.
        void Shop::RewardWisdomball(int player)
        {
            PlayerShop& ps = State(player);
            if (m_rng.Uniform(3) != 0)
                return;
            // The triple cards: the owner holds one copy short of a Golden, the tavern tier allows it, and the
            // tavern does not already show it.
            std::vector<int> triples;
            auto consider = [&](Minion const& m)
            {
                if (m.golden || m.card < 0 || CardAt(m.card).goldenId[0] == '\0' || m.Tier() > ps.tier)
                    return;
                if (std::find(triples.begin(), triples.end(), m.card) != triples.end())
                    return;
                for (Minion const& o : ps.offers)
                    if (o.card == m.card)
                        return;
                if (NormalCopies(player, m.card, nullptr) == TripleNeed(player, m.card) - 1)
                    triples.push_back(m.card);
            };
            for (Minion const& m : ps.board.Items())
                consider(m);
            for (Minion const& m : ps.hand.Items())
                consider(m);
            uint32_t mask = 0;
            for (Minion const& m : ps.board.Items())
                mask |= m.Tribes() & ~static_cast<uint32_t>(kTribeAll);
            if (triples.empty() && mask == 0)
                return;
            for (Minion& o : ps.offers)
            {
                int card = -1;
                while (!triples.empty() && card < 0)
                {
                    int const want = triples.back();
                    triples.pop_back();
                    int const pool = OwnPool(player, want);
                    if (pool >= 0 && m_pool.Take(want, pool))
                        card = want;
                }
                if (card < 0 && mask != 0)
                    card = m_pool.Draw(m_rng, ps.pools, kMinTier, ps.tier, mask);
                if (card < 0)
                    continue;
                ReturnToPool(player, o);
                o = MakeCopy(card, false, 1);
            }
            RecomputeOfferAuras(player);
        }

        void Shop::RewardBuy(int player, uint32_t uid)
        {
            PlayerShop& ps = State(player);
            for (int slot = 0; slot < 2; ++slot)
            {
                RewardState& r = slot == 1 ? ps.rewardState2 : ps.rewardState;
                RewardRule const* d = RewardFor(r);
                if (!d || d->kind != RewardKind::CookedBook)
                    continue;
                Minion* m = FindMinion(player, uid);
                if (!m)
                    continue;
                int const n = r.cookedBook;
                Buff(player, *m, n, n);
                r.cookedBook = n + 1;
            }
        }

        void Shop::RewardSell(int player, uint32_t uid)
        {
            PlayerShop& ps = State(player);
            for (int slot = 0; slot < 2; ++slot)
            {
                RewardState& r = slot == 1 ? ps.rewardState2 : ps.rewardState;
                RewardRule const* d = RewardFor(r);
                if (!d || d->kind != RewardKind::AnimaBribe)
                    continue;
                Minion* sold = FindMinion(player, uid);
                if (!sold || ps.offers.empty())
                    continue;
                Minion& o = ps.offers[m_rng.Range(0, static_cast<int>(ps.offers.size()) - 1)];
                Buff(player, o, sold->attack, sold->health);
            }
        }

        void Shop::RewardDiscover(int player, int card, bool golden)
        {
            PlayerShop const& ps = State(player);
            for (int slot = 0; slot < 2; ++slot)
            {
                RewardState const& r = slot == 1 ? ps.rewardState2 : ps.rewardState;
                RewardRule const* d = RewardFor(r);
                if (d && d->kind == RewardKind::SecretSinstone)
                    AddToHand(player, card, golden);
            }
        }

        // The Wheel of Yogg-Saron: five outcomes at 19% and Rod of Roasting at 5%.
        void Shop::Wheel(int player)
        {
            int const roll = m_rng.Range(1, 100);
            int const outcome = roll <= 95 ? (roll - 1) / 19 : 5;
            if (ValidPlayer(player))
            {
                std::vector<int>& spins = State(player).wheelSpins;   // note for the window's wheel
                spins.push_back(outcome);
                if (spins.size() > 8)
                    spins.erase(spins.begin());   // NPC seats are never drained: keep it bounded
            }
            WheelOutcome(player, outcome);
        }

        void Shop::WheelOutcome(int player, int outcome)
        {
            if (!ValidPlayer(player))
                return;
            PlayerShop& ps = State(player);
            switch (outcome)
            {
            case 0:   // Mysterybox: your Hero Power triggers an extra time
                if (RewardRule const* d = RewardFor(ps.rewardState); d && d->kind == RewardKind::Yogg)
                    ps.rewardState.powerExtraTrigger = true;
                else if (RewardRule const* d2 = RewardFor(ps.rewardState2); d2 && d2->kind == RewardKind::Yogg)
                    ps.rewardState2.powerExtraTrigger = true;
                else
                    ps.rewardState.powerExtraTrigger = true;   // a direct call without the Yogg reward (tests)
                break;
            case 1:   // Hand of Fate: a Discover of real Darkmoon Prizes, any Prize turn
                if (!PrizeDiscover(player, -1))
                    for (int k = 0; k < 2; ++k)   // no prize cards: a random minion instead
                        AddRandomToHand(player, kMinTier, kMaxTier, 0);
                break;
            case 2:   // Curse of Flesh: +3/+3, then shuffle the minions' stats
            {
                for (uint32_t uid : BoardUids(ps.board))
                    if (Minion* m = FindMinion(player, uid))
                        Buff(player, *m, 3, 3);
                // Shuffle the stats without the auras (they stay with the minion that has them): base Attack and base
                // max Health move together, and each minion ends at full health.
                std::vector<std::pair<int, int>> stats;
                for (Minion const& m : ps.board.Items())
                    stats.emplace_back(m.attack - m.auraAttack, m.maxHealth - m.auraHealth);
                m_rng.Shuffle(stats);
                for (int i = 0; i < ps.board.Size(); ++i)
                {
                    Minion& m = ps.board[i];
                    m.attack = stats[static_cast<std::size_t>(i)].first + m.auraAttack;
                    m.maxHealth = stats[static_cast<std::size_t>(i)].second + m.auraHealth;
                    m.health = m.maxHealth;
                }
                RecomputeBoard(player);
                break;
            }
            case 3:   // Mindflayer Goggles: steal the tavern, then restock
                StealTavern(player);
                break;
            case 4:   // Devouring Hunger: consume the tavern into your minions, then restock
            {
                std::vector<Minion> eaten;
                eaten.swap(ps.offers);   // taken off the tavern first: a buff hook cannot touch them mid-loop
                for (Minion const& o : eaten)
                    if (!ps.board.Empty())
                        Buff(player, ps.board[m_rng.Range(0, ps.board.Size() - 1)], o.attack, o.health);
                for (Minion const& o : eaten)
                    ReturnToPool(player, o);   // the consumed copies go back, so the pool conserves
                FillOffers(player);
                break;
            }
            default:  // Rod of Roasting: a run of +4/+4 until the "hero" is hit
            {
                int const hits = m_rng.Range(1, std::max(1, ps.board.Size()));
                for (int k = 0; k < hits && !ps.board.Empty(); ++k)
                    Buff(player, ps.board[m_rng.Range(0, ps.board.Size() - 1)], 4, 4);
                break;
            }
            }
            RecomputeOfferAuras(player);
        }
    }
}
