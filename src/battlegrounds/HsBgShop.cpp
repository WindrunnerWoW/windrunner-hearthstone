#include "HsBgShop.h"

#include <algorithm>
#include <cstring>

namespace Hs
{
    namespace Bg
    {
        int OffersForTier(int tier)
        {
            static int const offers[] = { 0, 3, 4, 4, 5, 5, 6 };
            return tier >= kMinTier && tier <= kMaxTier ? offers[tier] : 0;
        }

        int UpgradeBaseCost(int toTier)
        {
            static int const cost[] = { 0, 0, 5, 7, 8, 9, 11 };
            return toTier >= 2 && toTier <= kMaxTier ? cost[toTier] : 0;
        }

        int GoldForTurn(int turn) { return std::min(kMaxStartGold, kStartGold + std::max(0, turn - 1)); }

        char const* ResultName(ActionResult r)
        {
            switch (r)
            {
            case ActionResult::Ok: return "Ok";
            case ActionResult::BadPlayer: return "BadPlayer";
            case ActionResult::Eliminated: return "Eliminated";
            case ActionResult::GameOver: return "GameOver";
            case ActionResult::NotShopPhase: return "NotShopPhase";
            case ActionResult::DiscoverPending: return "DiscoverPending";
            case ActionResult::NoDiscover: return "NoDiscover";
            case ActionResult::BadIndex: return "BadIndex";
            case ActionResult::BadTarget: return "BadTarget";
            case ActionResult::NotEnoughGold: return "NotEnoughGold";
            case ActionResult::HandFull: return "HandFull";
            case ActionResult::BoardFull: return "BoardFull";
            case ActionResult::MaxTier: return "MaxTier";
            case ActionResult::NotAlly: return "NotAlly";
            case ActionResult::SendUsed: return "SendUsed";
            case ActionResult::NoPower: return "NoPower";
            case ActionResult::PowerUsed: return "PowerUsed";
            case ActionResult::NotPicking: return "NotPicking";
            }
            return "?";
        }

        Shop::Shop(PoolSet& pool, EffectRegistry const& effects, Rng& rng, int players,
                   HeroPowerRegistry const* heroPowers)
            : m_pool(pool), m_effects(effects),
              m_heroPowers(heroPowers ? *heroPowers : HeroPowerRegistry::Global()), m_rng(rng),
              m_players(static_cast<std::size_t>(players))
        {
        }

        HeroPowerDef const* Shop::Power(int player) const
        {
            if (!ValidPlayer(player))
                return nullptr;
            PlayerShop const& ps = State(player);
            // A power that replaced the hero's own (Azshara's conquest, Galewing's flightpath,
            // Sulfuras, a power swapped in by Nguyen or Finley) is the current power.
            if (!ps.power.powerOverride.empty())
                if (HeroPowerDef const* over = m_heroPowers.Find(ps.power.powerOverride))
                    return over;
            int const hero = ps.hero;
            if (hero < 0 || hero >= HeroCount())
                return nullptr;
            return m_heroPowers.Find(HeroAt(hero).powerId);
        }

        void Shop::SetHero(int player, int heroIndex)
        {
            if (!ValidPlayer(player))
                return;
            PlayerShop& ps = State(player);
            if (heroIndex < 0 || heroIndex >= HeroCount())
            {
                ps.hero = -1;
                return;
            }
            ps.hero = heroIndex;
            // A new hero starts with its own power, not one a previous hero had swapped in.
            ps.power.powerOverride.clear();
            ps.power.combatPower.clear();
            ps.power.combatPowerOnce = false;
            ps.power.powerCountdown = 0;
            // A new hero starts with no held minions, pot or tickets from a previous power.
            ps.power.holdsOffer = false;
            ps.power.heldOffer = Minion{};
            ps.power.heldTurns = 0;
            ps.power.expedition.clear();
            ps.power.potTypes = 0;
            ps.power.potCount = 0;
            ps.power.tickets = 0;
            int const h = HeroAt(heroIndex).health > 0 ? HeroAt(heroIndex).health : kStartHealth;
            ps.maxHealth = h;
            ps.health = h;
            RollArmor(player);
            // The Curator starts with a 1/1 Venomous Amalgam (Menagerist).
            if (HeroPowerDef const* const p = Power(player))
            {
                if (p->startMinion && p->startMinion[0] != '\0' && ps.board.Empty())
                {
                    int const card = FindCard(p->startMinion);
                    if (card >= 0)
                    {
                        SummonToBoard(player, card, false, 0);
                        Settle(player);
                    }
                }
                // Sir Finley discovers a hero power at the start of the game.
                if (p->startPowerChoice)
                    OfferPowerChoice(player, PowerChoiceKind::Swap, SwapPool(player), 3);
            }
        }

        void Shop::RollArmor(int player)
        {
            if (!ValidPlayer(player))
                return;
            PlayerShop& ps = State(player);
            if (ps.hero < 0 || ps.hero >= HeroCount())
            {
                ps.armor = 0;
                return;
            }
            int lo = 0;
            int hi = 0;
            ArmorTierRange(HeroAt(ps.hero).armorTier, lo, hi);
            ps.armor = m_rng.Range(lo, hi);
        }

        int Shop::UpgradeCost(int player) const
        {
            if (State(player).tier >= kMaxTier)
                return -1;
            HeroPowerDef const* const p = Power(player);
            return std::max(0, State(player).upgradeCost + (p ? p->upgradeCostDelta : 0));
        }

        void Shop::AfterPlay(int player, uint32_t uid, int card)
        {
            PlayerShop& ps = State(player);
            ++ps.power.playsThisTurn;
            if (card >= 0 && HasTribe(CardAt(card).tribes, kTribeElemental))
            {
                ++ps.power.elementalPlaysThisTurn;
                ++ps.power.elementalPlaysThisGame;
            }
            HeroPowerDef const* const p = Power(player);
            if (p && p->thirdPlayBuff && ps.power.playsThisTurn % 3 == 0)
            {
                Minion* m = FindMinion(player, uid);
                if (m)
                    Buff(player, *m, 2, 2);
            }
            // Chenvaala: every 3 Elementals lowers the next upgrade by 3.
            if (p && p->elementalPlaysPerUpgrade > 0 && ps.power.elementalPlaysThisGame > 0 &&
                ps.power.elementalPlaysThisGame % p->elementalPlaysPerUpgrade == 0)
                ps.upgradeCost = std::max(0, ps.upgradeCost - p->upgradeCostReduction);
        }

        ActionResult Shop::Gate(int player, bool discoverOk) const
        {
            if (!ValidPlayer(player))
                return ActionResult::BadPlayer;
            if (!discoverOk && (!State(player).options.empty() || !State(player).powerChoices.empty()))
                return ActionResult::DiscoverPending;
            return ActionResult::Ok;
        }

        Minion Shop::MakeCopy(int card, bool golden, int poolCopies)
        {
            Minion m = MakeMinion(card, golden, NewUid());
            m.poolCopies = poolCopies;
            return m;
        }

        // ------------------------------------------------------------------------------------------ hook dispatch

        void Shop::FireOn(int player, HookMember hook, uint32_t uid, uint32_t otherUid, uint32_t targetUid)
        {
            if (m_depth >= kMaxHookDepth)
                return;
            Minion* self = FindMinion(player, uid);
            if (!self)
                return;
            EffectDef const* e = m_effects.Find(self->card);
            if (!e || !(e->*hook))
                return;
            ShopCtx ctx(*this, player, self->golden);
            ctx.self = self;
            ctx.selfUid = uid;
            int const pos = State(player).board.IndexOfUid(uid);
            ctx.position = pos < 0 ? 0 : pos;
            if (otherUid != 0)
                ctx.other = FindMinion(player, otherUid);
            if (targetUid != 0)
                ctx.target = FindMinion(player, targetUid);
            ctx.playedCard = m_playedCard;
            ctx.playedGolden = m_playedGolden;
            ctx.magnetic = m_playedMagnetic;
            ctx.amountAttack = m_buffAttack;
            ctx.amountHealth = m_buffHealth;
            ShopHook const fn = e->*hook;
            ++m_depth;
            fn(ctx, ctx.golden);
            --m_depth;
        }

        void Shop::FireBoard(int player, HookMember hook, uint32_t exceptUid, uint32_t otherUid)
        {
            std::vector<uint32_t> uids;
            for (Minion const& m : State(player).board.Items())
                if (m.uid != exceptUid)
                    uids.push_back(m.uid);
            for (uint32_t uid : uids)
                FireOn(player, hook, uid, otherUid);
        }

        void Shop::FireQuest(int player, QuestEventInfo const& ev)
        {
            if (!ValidPlayer(player))
                return;
            ApplyQuestEvent(State(player).quest, ev);
            ApplyQuestEvent(State(player).quest2, ev);   // Denathrius's second quest counts too
            GameStats& stats = State(player).stats;      // the weeklies count the same events
            if (ev.kind == QuestEvent::StatsIncreased)
                stats.buffs += ev.amount;
            else if (ev.kind == QuestEvent::PlayBattlecry)
                stats.battlecries += ev.amount;
            else if (ev.kind == QuestEvent::Refresh)
                stats.refreshes += ev.amount;
            // A completed quest activates its reward. Inside an action the activation (which may add a card and Settle
            // the board, e.g. Pilfered Lamps merging pairs) waits until the action ends (ActionScope), so it never
            // changes the board under an effect that is still running.
            if (m_actionDepth == 0)
                CheckReward(player);
        }

        // "After you cast a spell" (Snail Cavalry, Eventide Brute, Athissa, Critter Wrangler). Every board
        // minion with the hook gets one context; `target` is the spell's target (nullptr for a spell without one).
        void Shop::FireSpellCast(int player, int spellCard, bool spellGolden, bool spellcraft, uint32_t targetUid)
        {
            if (m_depth >= kMaxHookDepth)
                return;
            std::vector<uint32_t> uids;
            for (Minion const& m : State(player).board.Items())
                uids.push_back(m.uid);
            for (uint32_t uid : uids)
            {
                Minion* self = FindMinion(player, uid);
                if (!self)
                    continue;
                EffectDef const* e = m_effects.Find(self->card);
                if (!e || !e->onSpellCast)
                    continue;
                ShopCtx ctx(*this, player, self->golden);
                ctx.self = self;
                ctx.selfUid = uid;
                int const pos = State(player).board.IndexOfUid(uid);
                ctx.position = pos < 0 ? 0 : pos;
                ctx.target = targetUid != 0 ? FindMinion(player, targetUid) : nullptr;
                ctx.playedCard = spellCard;
                ctx.playedGolden = spellGolden;
                ctx.spellCard = spellCard;
                ctx.spellcraft = spellcraft;
                ctx.spellPermanent = false;
                ++m_depth;
                e->onSpellCast(ctx, ctx.golden);
                --m_depth;
            }
        }

        // "After you spend 5 Gold" (Darkgaze Elder). Every board minion with the hook gets the amount.
        void Shop::FireGoldSpent(int player, int amount)
        {
            if (m_depth >= kMaxHookDepth || !ValidPlayer(player))
                return;
            std::vector<uint32_t> uids;
            for (Minion const& m : State(player).board.Items())
                uids.push_back(m.uid);
            for (uint32_t uid : uids)
            {
                Minion* self = FindMinion(player, uid);
                if (!self)
                    continue;
                EffectDef const* e = m_effects.Find(self->card);
                if (!e || !e->onSpendGold)
                    continue;
                ShopCtx ctx(*this, player, self->golden);
                ctx.self = self;
                ctx.selfUid = uid;
                int const pos = State(player).board.IndexOfUid(uid);
                ctx.position = pos < 0 ? 0 : pos;
                ctx.goldSpent = amount;
                ++m_depth;
                e->onSpendGold(ctx, ctx.golden);
                --m_depth;
            }
        }

        Shop::ActionScope::ActionScope(Shop& shop) : m_shop(shop)
        {
            ++m_shop.m_actionDepth;
        }

        Shop::ActionScope::~ActionScope()
        {
            if (--m_shop.m_actionDepth == 0)
                for (int p = 0; p < m_shop.Players(); ++p)   // every player: Send fills an ally's hand
                    m_shop.CheckReward(p);
        }

        // ------------------------------------------------------------------------------------------ pool bookkeeping

        // The pool of the player's faction a copy of `card` lives in. A player draws only from its own pool,
        // so a held minion always returns to it; a generated minion (poolCopies 0) needs no return at all.
        int Shop::OwnPool(int player, int card) const
        {
            return m_pool.HomeIn(card, State(player).pools);
        }

        void Shop::ReturnToPool(int player, Minion const& m)
        {
            int const pool = OwnPool(player, m.card);
            if (pool >= 0)
                m_pool.Return(m.card, pool, m.poolCopies);
            for (Attachment const& a : m.attachments)
            {
                int const ap = OwnPool(player, a.card);
                if (ap >= 0)
                    m_pool.Return(a.card, ap, a.poolCopies);
            }
        }

        void Shop::ReturnOffers(int player)
        {
            PlayerShop& ps = State(player);
            for (Minion const& m : ps.offers)
                ReturnToPool(player, m);
            ps.offers.clear();
        }

        // The two quest slots (see PlayerShop). Slot 0 is the standard quest, slot 1 Sire Denathrius's
        // second (start-of-game) one; the other heroes only ever use slot 0.
        QuestState const& Shop::QuestSlot(int player, int slot) const
        {
            static QuestState const none;
            if (!ValidPlayer(player))
                return none;
            PlayerShop const& ps = State(player);
            return slot == 1 ? ps.quest2 : ps.quest;
        }

        RewardState const& Shop::RewardSlot(int player, int slot) const
        {
            static RewardState const none;
            if (!ValidPlayer(player))
                return none;
            PlayerShop const& ps = State(player);
            return slot == 1 ? ps.rewardState2 : ps.rewardState;
        }

        int Shop::FreeQuestSlot(int player) const
        {
            if (!ValidPlayer(player))
                return -1;
            PlayerShop const& ps = State(player);
            if (ps.quest.quest < 0)
                return 0;
            if (ps.quest2.quest < 0)
                return 1;
            return -1;
        }

        int Shop::OfferQuestSlot(int player) const
        {
            if (!ValidPlayer(player))
                return -1;
            PlayerShop const& ps = State(player);
            if (ps.quest.quest < 0 && !ps.quest.offer.empty())
                return 0;
            if (ps.quest2.quest < 0 && !ps.quest2.offer.empty())
                return 1;
            return -1;
        }

        void Shop::FillOffers(int player)
        {
            PlayerShop& ps = State(player);
            int const want = OffersForTier(ps.tier) + ps.extraOffers;   // New Recruit's extra slots
            while (static_cast<int>(ps.offers.size()) < want)
            {
                int const card = m_pool.Draw(m_rng, ps.pools, kMinTier, ps.tier);
                if (card < 0)
                    break;
                ps.offers.push_back(MakeCopy(card, false, 1));
            }
            // Ysera: Bob always offers one extra minion of her tribe.
            if (HeroPowerDef const* const p = Power(player))
                if (p->extraOfferTribe != 0)
                {
                    int const card = m_pool.Draw(m_rng, ps.pools, kMinTier, ps.tier, p->extraOfferTribe);
                    if (card >= 0)
                        ps.offers.push_back(MakeCopy(card, false, 1));
                }
            RecomputeOfferAuras(player);
            MarkTicketOffer(player);   // Silas Darkmoon's ticket on one of the new offers
        }

        // Silas Darkmoon. One offer carries the ticket. A ticketed offer that stays (a Frozen one from the
        // last turn) keeps it; a new offer is marked only when no ticket is left in the tavern.
        void Shop::MarkTicketOffer(int player)
        {
            if (!ValidPlayer(player))
                return;
            PlayerShop& ps = State(player);
            HeroPowerDef const* const p = Power(player);
            if (!p || !p->ticketsInTavern)
            {
                for (Minion& o : ps.offers)
                    o.flags &= ~kMinionTicket;
                return;
            }
            if (ps.offers.empty())
                return;
            for (Minion const& o : ps.offers)
                if ((o.flags & kMinionTicket) != 0)
                    return;
            std::size_t const pick = m_rng.Uniform(static_cast<uint32_t>(ps.offers.size()));
            ps.offers[pick].flags |= kMinionTicket;
        }

        void Shop::StripAura(Minion& m)
        {
            m.attack -= m.auraAttack;
            m.maxHealth -= m.auraHealth;
            m.health = std::max(1, m.health - m.auraHealth);
            if (m.health > m.maxHealth)
                m.health = m.maxHealth;
            m.auraAttack = 0;
            m.auraHealth = 0;
        }

        // An offer leaves the tavern (bought, taken): every tavern bonus (Legion Overseer, Millificent, Alter Ego, Teal Tiger
        // Sapphire, ...) is the minion's own from then on (a bought minion keeps what Bob's Tavern gave
        // it). The stats already include the bonus, so only the aura bookkeeping is cleared.
        void Shop::LeaveTavern(int player, Minion& m)
        {
            m.flags &= ~kMinionFrozen;
            m.flags &= ~kMinionTicket;   // a ticket is only on a tavern offer
            // Salty Looter's own count is tracked by counters[1] / [2] once it is in hand or on the board
            // (ApplyPirateSummonBonuses), so its share of the offer bonus is removed, not kept.
            if (ValidPlayer(player) && m.card >= 0)
                if (EffectDef const* const e = m_effects.Find(m.card))
                    if (e->perPirateSummonedAttack != 0 || e->perPirateSummonedHealth != 0)
                    {
                        int const per = m.golden ? 2 : 1;
                        int const sa = e->perPirateSummonedAttack * State(player).piratesSummonedThisGame * per;
                        int const sh = e->perPirateSummonedHealth * State(player).piratesSummonedThisGame * per;
                        m.attack -= sa;
                        m.maxHealth -= sh;
                        m.health = std::max(1, m.health - sh);
                    }
            m.auraAttack = 0;
            m.auraHealth = 0;
        }

        // "Minions in Bob's Tavern have +2/+2": the bonus rides on the aura fields of the offers, so a bought or taken
        // offer is stripped (StripAura) and a consumed offer shows its bonus (Felbat eats the buffed stats).
        void Shop::RecomputeOfferAuras(int player)
        {
            PlayerShop& ps = State(player);
            int bonusAttack = 0;
            int bonusHealth = 0;
            for (Minion const& m : ps.board.Items())
            {
                EffectDef const* e = m_effects.Find(m.card);
                if (!e)
                    continue;
                bool const g = m.golden;
                bonusAttack += g && e->tavernAttackGolden != 0 ? e->tavernAttackGolden : e->tavernAttack;
                bonusHealth += g && e->tavernHealthGolden != 0 ? e->tavernHealthGolden : e->tavernHealth;
            }
            RewardRule const* const rd = RewardFor(ps.rewardState);
            if (rd && rd->tavernBonusPerRefresh != 0)
            {
                bonusAttack += rd->tavernBonusPerRefresh * ps.rewardState.refreshesThisTurn;   // Teal Tiger Sapphire
                bonusHealth += rd->tavernBonusPerRefresh * ps.rewardState.refreshesThisTurn;
            }
            RewardRule const* const rd2 = RewardFor(ps.rewardState2);   // Denathrius's second reward
            if (rd2 && rd2->tavernBonusPerRefresh != 0)
            {
                bonusAttack += rd2->tavernBonusPerRefresh * ps.rewardState2.refreshesThisTurn;
                bonusHealth += rd2->tavernBonusPerRefresh * ps.rewardState2.refreshesThisTurn;
            }
            bonusAttack += ps.tavernAttackAll;   // The Good Stuff (every offer, for the rest of the game)
            for (Minion& o : ps.offers)
            {
                int ba = bonusAttack;
                int bh = bonusHealth;
                if (HasTribe(o.Tribes(), kTribeElemental))
                {
                    // Nomi / Dazzling Lightspawn, only on Elementals in Bob's Tavern.
                    ba += ps.elementalTavernAttack;
                    bh += ps.elementalTavernHealth;
                }
                if (rd && rd->alterEgoBonus != 0)
                {
                    bool const oddTier = (o.Tier() % 2) == 1;   // Alter Ego: +6/+6 on the active tier parity
                    if (oddTier == ps.rewardState.alterEgoOdd)
                    {
                        ba += rd->alterEgoBonus;
                        bh += rd->alterEgoBonus;
                    }
                }
                if (rd2 && rd2->alterEgoBonus != 0)
                {
                    bool const oddTier = (o.Tier() % 2) == 1;
                    if (oddTier == ps.rewardState2.alterEgoOdd)
                    {
                        ba += rd2->alterEgoBonus;
                        bh += rd2->alterEgoBonus;
                    }
                }
                if (EffectDef const* const e = m_effects.Find(o.card))
                    if (e->perPirateSummonedAttack != 0 || e->perPirateSummonedHealth != 0)
                    {
                        // Salty Looter: "wherever this is" includes the Tavern.
                        int const per = o.golden ? 2 : 1;
                        ba += e->perPirateSummonedAttack * ps.piratesSummonedThisGame * per;
                        bh += e->perPirateSummonedHealth * ps.piratesSummonedThisGame * per;
                    }
                o.attack += ba - o.auraAttack;
                o.maxHealth += bh - o.auraHealth;
                o.health += bh - o.auraHealth;
                if (o.health < 1)
                    o.health = 1;
                o.auraAttack = ba;
                o.auraHealth = bh;
            }
        }

        std::vector<int> Shop::HeldCopies() const
        {
            std::vector<int> held(static_cast<std::size_t>(CardCount()), 0);
            auto add = [&held](Minion const& m)
            {
                if (m.card < 0)
                    return;
                held[static_cast<std::size_t>(m.card)] += m.poolCopies;
                for (Attachment const& a : m.attachments)
                    held[static_cast<std::size_t>(a.card)] += a.poolCopies;
            };
            for (PlayerShop const& ps : m_players)
            {
                for (Minion const& m : ps.board.Items())
                    add(m);
                for (Minion const& m : ps.hand.Items())
                    add(m);
                for (Minion const& m : ps.offers)
                    add(m);
                for (Minion const& m : ps.options)
                    add(m);
                // Maiev's held offer and Faelin's Tier minions keep their pool copies too.
                if (ps.power.holdsOffer)
                    add(ps.power.heldOffer);
                for (Minion const& m : ps.power.expedition)
                    add(m);
            }
            return held;
        }

        bool Shop::CheckConservation(std::string* why) const
        {
            std::vector<int> const held = HeldCopies();
            for (int card = 0; card < CardCount(); ++card)
            {
                int const have = m_pool.Left(card) + held[static_cast<std::size_t>(card)];
                if (have != m_pool.Initial(card))
                {
                    if (why)
                        *why = std::string(CardAt(card).id) + ": pool " + std::to_string(m_pool.Left(card)) + " + held " +
                            std::to_string(held[static_cast<std::size_t>(card)]) + " != " + std::to_string(m_pool.Initial(card));
                    return false;
                }
            }
            return true;
        }

        // ------------------------------------------------------------------------------------------ hand and triples

        int Shop::NormalCopies(int player, int card, int* inHand) const
        {
            PlayerShop const& ps = State(player);
            int n = 0;
            int h = 0;
            for (Minion const& m : ps.hand.Items())
                if (m.card == card && !m.golden)
                {
                    ++n;
                    ++h;
                }
            for (Minion const& m : ps.board.Items())
                if (m.card == card && !m.golden)
                    ++n;
            if (inHand)
                *inHand = h;
            return n;
        }

        int Shop::TripleNeed(int player, int card) const
        {
            PlayerShop const& ps = State(player);
            RewardRule const* const d = RewardFor(ps.rewardState);
            RewardRule const* const d2 = RewardFor(ps.rewardState2);   // Denathrius's second reward
            if ((d && d->twoForGolden) || (d2 && d2->twoForGolden))
                return 2;   // Pilfered Lamps
            if (card >= 0)
            {
                EffectDef const* const e = m_effects.Find(card);
                if (e && e->goldenFromTwo)
                    return 2;   // First Mate Pip
            }
            return kTripleSize;
        }

        // Room for a minion of this card in the hand. A full hand still takes a normal minion that completes a triple
        // with at least one copy in the hand: the merge frees a slot at once.
        bool Shop::CanReceive(int player, int card, bool golden) const
        {
            PlayerShop const& ps = State(player);
            if (!ps.hand.Full())
                return true;
            if (golden || CardAt(card).role == CardRole::Spell || CardAt(card).goldenId[0] == '\0')
                return false;
            int inHand = 0;
            int const n = NormalCopies(player, card, &inHand);
            return n == TripleNeed(player, card) - 1 && inHand >= 1;
        }

        // Salty Looter: "Has +X/+Y for each Pirate you've summoned this game (wherever this is)". counters[1] and
        // counters[2] remember the bonus already applied, so a recount only adds the difference.
        void Shop::ApplyPirateSummonBonuses(int player)
        {
            if (!ValidPlayer(player))
                return;
            PlayerShop& ps = State(player);
            auto apply = [&](Minion& m)
            {
                if (m.card < 0)
                    return;
                EffectDef const* const e = m_effects.Find(m.card);
                if (!e || (e->perPirateSummonedAttack == 0 && e->perPirateSummonedHealth == 0))
                    return;
                int const per = m.golden ? 2 : 1;
                int const wantA = e->perPirateSummonedAttack * ps.piratesSummonedThisGame * per;
                int const wantH = e->perPirateSummonedHealth * ps.piratesSummonedThisGame * per;
                int const da = wantA - m.counters[1];
                int const dh = wantH - m.counters[2];
                m.counters[1] = wantA;
                m.counters[2] = wantH;
                if (da != 0 || dh != 0)
                    Buff(player, m, da, dh);
            };
            for (Minion& m : ps.board.Items())
                apply(m);
            for (Minion& m : ps.hand.Items())
                apply(m);
        }

        void Shop::CountPirateSummoned(int player, int card)
        {
            if (!ValidPlayer(player) || card < 0)
                return;
            if (!HasTribe(CardAt(card).tribes, kTribePirate))
                return;
            ++State(player).piratesSummonedThisGame;
            ApplyPirateSummonBonuses(player);
            RecomputeOfferAuras(player);   // a Salty Looter in Bob's Tavern counts too
        }

        void Shop::CountCombatPirateSummons(int player, int count)
        {
            if (!ValidPlayer(player) || count <= 0)
                return;
            State(player).piratesSummonedThisGame += count;
            ApplyPirateSummonBonuses(player);
            RecomputeOfferAuras(player);
        }

        bool Shop::PushToHand(int player, Minion const& m)
        {
            PlayerShop& ps = State(player);
            if (!CanReceive(player, m.card, m.golden))
                return false;
            if (ps.hand.Full())
                ps.hand.Items().push_back(m); // one over the cap until ResolveTriples merges (see CanReceive)
            else
                ps.hand.Push(m);
            FireQuest(player, QuestEventInfo{ QuestEvent::CardToHand });   // cards added to hand
            FireBoard(player, &EffectDef::onCardToHand);                     // Peggy Brittlebone
            ApplyPirateSummonBonuses(player);                                // a Salty Looter entering the hand
            return true;
        }

        void Shop::ResolveTriples(int player)
        {
            PlayerShop& ps = State(player);
            for (;;)
            {
                int card = -1;
                for (int zone = 0; zone < 2 && card < 0; ++zone)
                {
                    std::vector<Minion> const& items = zone == 0 ? ps.hand.Items() : ps.board.Items();
                    for (Minion const& m : items)
                    {
                        if (m.golden || m.card < 0 || CardAt(m.card).role == CardRole::Spell || CardAt(m.card).goldenId[0] == '\0')
                            continue;
                        int const need = TripleNeed(player, m.card);
                        int inHand = 0;
                        if (NormalCopies(player, m.card, &inHand) < need)
                            continue;
                        int const takenFromHand = std::min(inHand, need);
                        if (ps.hand.Size() - takenFromHand + 1 > kHandMax)
                            continue; // no room for the golden minion yet
                        card = m.card;
                        break;
                    }
                }
                if (card < 0)
                    return;

                int const need = TripleNeed(player, card);
                // Take the copies, hand first, then the board, left to right.
                std::vector<int> handIdx;
                std::vector<int> boardIdx;
                for (int i = 0; i < ps.hand.Size() && static_cast<int>(handIdx.size()) < need; ++i)
                    if (ps.hand[i].card == card && !ps.hand[i].golden)
                        handIdx.push_back(i);
                for (int i = 0; i < ps.board.Size() && static_cast<int>(handIdx.size() + boardIdx.size()) < need; ++i)
                    if (ps.board[i].card == card && !ps.board[i].golden)
                        boardIdx.push_back(i);

                CardDef const& d = CardAt(card);
                Minion g = MakeMinion(card, true, NewUid());
                EffectDef const* const saltyDef = m_effects.Find(card);
                bool const salty = saltyDef && (saltyDef->perPirateSummonedAttack != 0 || saltyDef->perPirateSummonedHealth != 0);
                auto absorb = [&](Minion const& c)
                {
                    // Buffs beyond the base card carry over (aura bonuses do not); extra keywords are kept.
                    int const saltyA = salty ? c.counters[1] : 0;   // Salty Looter: the golden rate replaces these
                    int const saltyH = salty ? c.counters[2] : 0;
                    g.Buff(c.attack - c.auraAttack - d.attack - saltyA, c.maxHealth - c.auraHealth - d.health - saltyH);
                    uint32_t extra = c.keywords & ~d.keywords;
                    if (g.keywords & kKwMegaWindfury)
                        extra &= ~static_cast<uint32_t>(kKwWindfury);
                    g.keywords |= extra;
                    for (Attachment const& a : c.attachments)
                        g.attachments.push_back(a);
                    // "until next turn" spell stats / keywords stay temporary on the golden minion.
                    g.tempAttack += c.tempAttack;
                    g.tempHealth += c.tempHealth;
                    g.tempKeywords |= c.tempKeywords & g.keywords;
                    g.gruulRules = g.gruulRules || c.gruulRules;   // Gruul Rules stays on the triple
                    for (int k = 0; k < kMinionCounters; ++k)
                        g.counters[k] += c.counters[k];
                    g.poolCopies += c.poolCopies;
                };
                for (int i : handIdx)
                    absorb(ps.hand[i]);
                for (int i : boardIdx)
                    absorb(ps.board[i]);
                auto const absorbGems = [&](Minion const& c)
                {
                    g.bloodGemAttack += c.bloodGemAttack;
                    g.bloodGemHealth += c.bloodGemHealth;
                };
                for (int i : handIdx)
                    absorbGems(ps.hand[i]);
                for (int i : boardIdx)
                    absorbGems(ps.board[i]);
                for (auto it = handIdx.rbegin(); it != handIdx.rend(); ++it)
                    ps.hand.Remove(*it);
                for (auto it = boardIdx.rbegin(); it != boardIdx.rend(); ++it)
                    ps.board.Remove(*it);
                if (EffectDef const* const e = m_effects.Find(card))
                    if (e->perPirateSummonedAttack != 0 || e->perPirateSummonedHealth != 0)
                    {
                        g.counters[1] = 0;   // Salty Looter: the golden rate replaces the copies' bonuses
                        g.counters[2] = 0;
                    }
                ps.hand.Push(g);
                ++ps.stats.triples;
                RecomputeBoard(player);
                ApplyPirateSummonBonuses(player);
            }
        }

        void Shop::Settle(int player)
        {
            ResolveTriples(player);
            PlayerShop& ps = State(player);
            // Safety net: a hook that removed a triple partner after a full-hand buy leaves the hand one over.
            while (ps.hand.Size() > kHandMax)
            {
                Minion m = ps.hand.Remove(ps.hand.Size() - 1);
                ReturnToPool(player, m);
            }
            RecomputeBoard(player);
            RecomputeOfferAuras(player);
        }

        // ------------------------------------------------------------------------------------------ Discover

        void Shop::ActivateDiscover(int player)
        {
            PlayerShop& ps = State(player);
            while (ps.options.empty() && ps.powerChoices.empty() && !ps.queue.empty())
            {
                DiscoverRequest const req = ps.queue.front();
                ps.queue.erase(ps.queue.begin());
                ps.optionsSecret = false;
                ps.optionsHolmes = -1;
                if (req.holmesCard >= 0)
                {
                    // Murloc Holmes: two minions, one of them was on the opponent's board. The decoy is a pool minion;
                    // neither goes to the hand (a right guess is a Coin), so the real one is a generated copy.
                    ps.optionsHold = false;
                    ps.optionsHolmes = req.holmesCard;
                    std::vector<int> const decoys = m_pool.DrawDistinct(m_rng, ps.pools, kMinTier, kMaxTier, 0, 2);
                    int decoy = -1;
                    for (int c : decoys)
                        if (decoy < 0 && c != req.holmesCard)
                            decoy = c;
                    for (int c : decoys)
                        if (c != decoy)
                            m_pool.Return(c, OwnPool(player, c), 1);   // an unused draw goes back
                    ps.options.push_back(MakeCopy(req.holmesCard, false, 0));
                    if (decoy >= 0)
                        ps.options.push_back(MakeCopy(decoy, false, 1));
                    if (m_rng.Coin())
                        std::swap(ps.options[0], ps.options[ps.options.size() - 1]);
                    continue;
                }
                if (!req.powerOptions.empty())
                {
                    // A choice of hero powers (Galewing, Bru'kan, Tavish, Nguyen, Finley).
                    std::vector<std::string> ids = req.powerOptions;
                    m_rng.Shuffle(ids);
                    int const count = std::max(1, std::min(req.powerCount, static_cast<int>(ids.size())));
                    ids.resize(static_cast<std::size_t>(count));
                    ps.powerChoices = ids;
                    ps.choiceKind = req.powerKind;
                    continue;
                }
                if (req.secret)
                {
                    // A Discover of Secrets the player does not have yet (The Great Akazamzarak).
                    ps.optionsHold = false;
                    ps.optionsSecret = true;
                    std::vector<int> cards;
                    for (int c : SecretCardIds())
                        if (std::find(ps.secrets.begin(), ps.secrets.end(), c) == ps.secrets.end())
                            cards.push_back(c);
                    m_rng.Shuffle(cards);
                    if (static_cast<int>(cards.size()) > kDiscoverOptions)
                        cards.resize(static_cast<std::size_t>(kDiscoverOptions));
                    for (int c : cards)
                        ps.options.push_back(MakeCopy(c, false, 0));
                    continue;
                }
                if (req.prizeTurn != 0)
                {
                    // The Darkmoon Prizes of a Prize turn (Tickatus, Hand of Fate). The options are spell
                    // cards; the chosen one goes to the hand and is cast later, like any other spell.
                    ps.optionsHold = false;
                    std::vector<int> cards = DarkmoonPrizeIds(req.prizeTurn);
                    // Big Winner! pays one Discover per earlier Prize turn: with none yet it would do nothing.
                    if (DarkmoonPrizeTurnForRound(m_turn) <= 1)
                        cards.erase(std::remove(cards.begin(), cards.end(), FindCard("BGS_Treasures_032")), cards.end());
                    m_rng.Shuffle(cards);
                    if (static_cast<int>(cards.size()) > kDiscoverOptions)
                        cards.resize(static_cast<std::size_t>(kDiscoverOptions));
                    for (int c : cards)
                        ps.options.push_back(MakeCopy(c, false, 0));
                    continue;
                }
                if (req.spell)
                {
                    // Three distinct Spellcraft spells of the tier range (Lady Vashj).
                    ps.optionsHold = false;
                    std::vector<int> cards = SpellcraftSpells(req.maxTier);
                    m_rng.Shuffle(cards);
                    if (static_cast<int>(cards.size()) > kDiscoverOptions)
                        cards.resize(static_cast<std::size_t>(kDiscoverOptions));
                    for (int c : cards)
                    {
                        Minion m = MakeCopy(c, false, 0);
                        m.permanentSpell = req.permanentSpell;
                        ps.options.push_back(m);
                    }
                    continue;
                }
                if (req.boardCopy)
                {
                    // Three plain copies from the last opponent's warband (Scabbs Cutterbutter).
                    ps.optionsHold = false;
                    std::vector<Minion> const& warband = ps.lastOpponent.Items();
                    std::vector<int> order(static_cast<std::size_t>(warband.size()));
                    for (int i = 0; i < static_cast<int>(order.size()); ++i)
                        order[static_cast<std::size_t>(i)] = i;
                    m_rng.Shuffle(order);
                    if (static_cast<int>(order.size()) > kDiscoverOptions)
                        order.resize(static_cast<std::size_t>(kDiscoverOptions));
                    for (int i : order)
                    {
                        // Plain: base stats, no buffs, hats or golden (the opponent's per-instance state stays behind).
                        ps.options.push_back(MakeCopy(warband[static_cast<std::size_t>(i)].card, false, 0));
                    }
                    continue;
                }
                if (!req.warband.empty())
                {
                    // Mr. Bigglesworth. Up to three copies of the dead warband, keeping their stats and
                    // enchantments. They are generated copies (poolCopies 0): the dead player's own copies go back to
                    // the pool when the elimination releases the board, so the pool count stays right.
                    ps.optionsHold = false;
                    std::vector<int> order(static_cast<std::size_t>(req.warband.size()));
                    for (int i = 0; i < static_cast<int>(order.size()); ++i)
                        order[static_cast<std::size_t>(i)] = i;
                    m_rng.Shuffle(order);
                    int const want = req.optionCount > 0 ? req.optionCount : kDiscoverOptions;
                    if (static_cast<int>(order.size()) > want)
                        order.resize(static_cast<std::size_t>(want));
                    for (int i : order)
                    {
                        Minion m = req.warband[static_cast<std::size_t>(i)];
                        m.uid = NewUid();
                        m.poolCopies = 0;
                        m.flags = 0;
                        for (Attachment& a : m.attachments)
                            a.poolCopies = 0;
                        ps.options.push_back(m);
                    }
                    continue;
                }
                int const options = req.optionCount > 0 ? req.optionCount : kDiscoverOptions;
                std::vector<int> const cards =
                    m_pool.DrawDistinct(m_rng, ps.pools, req.minTier, req.maxTier, req.tribeAny, options);
                ps.optionsHold = req.hold;
                for (int c : cards)
                    ps.options.push_back(MakeCopy(c, false, 1));
            }
        }

        void Shop::QueueDiscover(int player, DiscoverRequest const& request)
        {
            State(player).queue.push_back(request);
            ActivateDiscover(player);
        }

        bool Shop::QueueCustomDiscover(int player, DiscoverRequest const& request)
        {
            if (!ValidPlayer(player))
                return false;
            QueueDiscover(player, request);
            return true;
        }

        // Maiev Shadowsong. The offer leaves the tavern with its pool copy and waits in the power's held
        // slot; StartTurn moves it to the hand with +2/+2 after three turns.
        bool Shop::HoldOffer(int player, int offerIndex)
        {
            if (!ValidPlayer(player))
                return false;
            PlayerShop& ps = State(player);
            if (ps.power.holdsOffer || offerIndex < 0 || offerIndex >= static_cast<int>(ps.offers.size()))
                return false;
            Minion m = ps.offers[static_cast<std::size_t>(offerIndex)];
            ps.offers.erase(ps.offers.begin() + offerIndex);
            m.flags = 0;
            LeaveTavern(player, m);
            ps.power.heldOffer = m;
            ps.power.holdsOffer = true;
            ps.power.heldTurns = 3;
            RecomputeOfferAuras(player);
            Settle(player);
            return true;
        }

        void Shop::OfferWarbandDiscover(int player, Board const& warband)
        {
            if (!ValidPlayer(player))
                return;
            if (warband.Empty())
            {
                AddSpellToHand(player, FindCard("TB_BaconShop_HP_080t"), false, false, false);   // Consolation Coin
                return;
            }
            DiscoverRequest req;
            req.warband = warband.Items();
            req.optionCount = std::min(kDiscoverOptions, warband.Size());
            QueueDiscover(player, req);
        }

        // The open hero-power choice, the swap pool and the queueing of a choice.
        std::vector<std::string> const& Shop::PowerChoices(int player) const
        {
            static std::vector<std::string> const none;
            return ValidPlayer(player) ? State(player).powerChoices : none;
        }

        std::vector<std::string> Shop::SwapPool(int player) const
        {
            // Every curated power but Conviction, Power of the Storm and Sharpen Blades, and the powers that need a
            // minion type this lobby lacks (point 3).
            std::vector<std::string> out;
            if (!ValidPlayer(player))
                return out;
            uint32_t lobby = 0;
            for (uint32_t t : PlayerLobbyTribes(player))
                lobby |= t;
            for (int i = 0; i < HeroCount(); ++i)
            {
                if (!HeroAt(i).curated)
                    continue;
                std::string const id = HeroAt(i).powerId;
                if (id == "BG21_HERO_000p" || id == "BG20_HERO_202p" || id == "TB_BaconShop_HP_001")
                    continue;
                // The powers that would be dead picks once the game has started (owner point 3):
                // Patchwerk's health is on the hero row; The Curator and N'Zoth start with a minion; A. F. Kay,
                // Faelin, Denathrius and Finley act at the start of the game only.
                if (id == "TB_BaconShop_HP_035" || id == "TB_BaconShop_HP_033" || id == "TB_BaconShop_HP_105" ||
                    id == "TB_BaconShop_HP_044" || id == "BG22_HERO_201p" || id == "BG24_HERO_100p" ||
                    id == "TB_BaconShop_HP_057")
                    continue;
                HeroPowerDef const* const def = m_heroPowers.Find(id);
                if (!def)
                    continue;
                if (def->needsLobbyTribe != 0 && (lobby & def->needsLobbyTribe) == 0)
                    continue;
                if (std::find(out.begin(), out.end(), id) == out.end())
                    out.push_back(id);
            }
            return out;
        }

        bool Shop::OfferPowerChoice(int player, PowerChoiceKind kind, std::vector<std::string> const& pool, int count)
        {
            if (!ValidPlayer(player) || kind == PowerChoiceKind::None || pool.empty())
                return false;
            DiscoverRequest req;
            req.powerKind = kind;
            req.powerOptions = pool;
            req.powerCount = std::max(1, count);
            QueueDiscover(player, req);
            return true;
        }

        std::string Shop::CombatPowerId(int player) const
        {
            if (!ValidPlayer(player))
                return std::string();
            PlayerShop const& ps = State(player);
            if (!ps.power.combatPower.empty())
                return ps.power.combatPower;
            HeroPowerDef const* const p = Power(player);
            return p ? std::string(p->id) : std::string();
        }

        void Shop::CountEnemyKills(int player, int kills)
        {
            if (!ValidPlayer(player) || kills <= 0)
                return;
            PlayerShop& ps = State(player);
            HeroPowerDef const* const p = Power(player);
            if (!p || p->killsForSulfuras <= 0 || std::string(p->id) != "TB_BaconShop_HP_087")
                return;
            ps.power.enemyKills += kills;
            if (ps.power.enemyKills >= p->killsForSulfuras)
            {
                // Ragnaros: DIE, INSECTS! becomes Sulfuras (the passive that buffs the ends at EndTurn).
                if (HeroPowerDef const* const sulfuras = m_heroPowers.Find("TB_BaconShop_HP_087t"))
                    ps.power.powerOverride = sulfuras->id;
            }
        }

        ActionResult Shop::Choose(int player, int optionIndex)
        {
            ActionScope const scope(*this);   // a reward a quest finishes here turns on at the end
            ActionResult r = Gate(player, true);
            if (r != ActionResult::Ok)
                return r;
            PlayerShop& ps = State(player);
            if (!ps.powerChoices.empty())
            {
                // A chosen hero power.
                if (optionIndex < 0 || optionIndex >= static_cast<int>(ps.powerChoices.size()))
                    return ActionResult::BadIndex;
                std::string const chosen = ps.powerChoices[static_cast<std::size_t>(optionIndex)];
                PowerChoiceKind const kind = ps.choiceKind;
                ps.powerChoices.clear();
                ps.choiceKind = PowerChoiceKind::None;
                HeroPowerDef const* const def = m_heroPowers.Find(chosen);
                if (kind == PowerChoiceKind::Stance)
                {
                    // Cariel Roame: the improvement is kept for the rest of the game.
                    if (chosen == kStanceUnited)
                        ++ps.power.stanceUnited;
                    else if (chosen == kStanceOffense)
                        ++ps.power.stanceOffense;
                    else if (chosen == kStanceDefense)
                        ++ps.power.stanceDefense;
                }
                else if (def && kind == PowerChoiceKind::Flightpath)
                {
                    ps.power.powerOverride = def->id;
                    ps.power.powerCountdown = std::max(1, def->countdownTurns);
                }
                else if (def && kind == PowerChoiceKind::Invocation)
                    ps.power.combatPower = def->id;   // Bru'kan: stays until changed
                else if (def && kind == PowerChoiceKind::Aim)
                {
                    ps.power.combatPower = def->id;   // Tavish: the next combat only
                    ps.power.combatPowerOnce = true;
                }
                else if (def)
                {
                    ps.power.powerOverride = def->id; // Swap: Finley, Nguyen
                    ps.power.powerCountdown = 0;
                    ps.power.combatPower.clear();     // the old power's invocation / aim goes with it
                    ps.power.combatPowerOnce = false;
                }
                Settle(player);
                ActivateDiscover(player);
                return ActionResult::Ok;
            }
            if (ps.options.empty())
                return ActionResult::NoDiscover;
            if (optionIndex < 0 || optionIndex >= static_cast<int>(ps.options.size()))
                return ActionResult::BadIndex;
            Minion chosen = ps.options[static_cast<std::size_t>(optionIndex)];
            for (int i = 0; i < static_cast<int>(ps.options.size()); ++i)
                if (i != optionIndex)
                    ReturnToPool(player, ps.options[static_cast<std::size_t>(i)]);
            ps.options.clear();
            bool const hold = ps.optionsHold;
            bool const secret = ps.optionsSecret;
            int const holmes = ps.optionsHolmes;
            ps.optionsHold = false;
            ps.optionsSecret = false;
            ps.optionsHolmes = -1;
            if (holmes >= 0)
            {
                // Murloc Holmes: a right guess is a Gold Coin; the chosen minion itself is not kept.
                ReturnToPool(player, chosen);
                if (chosen.card == holmes)
                    AddSpellToHand(player, FindCard("TB_BaconShop_HP_008a"), false, false, false);
                Settle(player);
                ActivateDiscover(player);
                return ActionResult::Ok;
            }
            if (secret)
            {
                // The Great Akazamzarak. The Secret goes into play, not the hand (the render: "Put it
                // into the battlefield"); the cap is 5.
                if (static_cast<int>(ps.secrets.size()) < 5 && chosen.card >= 0)
                    ps.secrets.push_back(chosen.card);
                Settle(player);
                ActivateDiscover(player);
                return ActionResult::Ok;
            }
            if (hold)
            {
                // Ambassador Faelin's Tier minions wait for their Tier (PowerState::expedition) and keep
                // their pool copies. The power button shows them.
                ps.power.expedition.push_back(chosen);
            }
            // A full hand loses the minion (its copy goes back to the pool).
            bool const kept = hold || PushToHand(player, chosen);
            if (!kept)
                ReturnToPool(player, chosen);
            else if (!hold)
                RewardDiscover(player, chosen.card, chosen.golden);   // Secret Sinstone
            Settle(player);
            ActivateDiscover(player);
            return ActionResult::Ok;
        }

        // ------------------------------------------------------------------------------------------ turn flow

        void Shop::StartTurn(int player, int turn)
        {
            ActionScope const scope(*this);   // a reward a quest finishes here turns on at the end
            if (!ValidPlayer(player))
                return;
            PlayerShop& ps = State(player);
            m_turn = turn;
            ps.skipTurn = false;
            // "until next turn" spell stats / keywords end here; Spellcraft cards from the last turn leave
            // the hand (before this turn's spells are added).
            ClearTemporary(player);
            // Competitive Spirit (a Secret) gives the board +1/+1 when the owner's turn starts, then
            // leaves play.
            {
                int const spirit = FindCard("TB_Bacon_Secrets_13");
                auto const it = std::find(ps.secrets.begin(), ps.secrets.end(), spirit);
                if (spirit >= 0 && it != ps.secrets.end())
                {
                    ps.secrets.erase(it);
                    for (Minion& m : ps.board.Items())
                        Buff(player, m, 1, 1);
                }
            }
            ps.power.barovGuess = -1;
            // Master Nguyen's swapped power lasts one round (see below, after the flightpath countdown).
            HeroPowerDef const* basePower = nullptr;
            if (ps.hero >= 0 && ps.hero < HeroCount())
                basePower = m_heroPowers.Find(HeroAt(ps.hero).powerId);
            bool const swapThisTurn = basePower && basePower->swapEachTurn;
            // Stealth (Theotar's Parasol) and Reborn (The Lich King) granted "until next turn" end at the
            // owner's next turn start. A card whose own keywords include one keeps it.
            for (Minion& m : ps.board.Items())
                if (m.card >= 0)
                {
                    uint32_t const own = CardAt(m.card).keywords;
                    if ((own & kKwStealth) == 0)
                        m.keywords &= ~static_cast<uint32_t>(kKwStealth);
                    if ((own & kKwReborn) == 0)
                        m.keywords &= ~static_cast<uint32_t>(kKwReborn);
                }
            ps.gold = GoldForTurn(turn);
            ps.sentThisTurn = false;
            // The this-turn prize effects end here.
            ps.battlecriesTwice = false;
            ps.unlimitedCoinsOut = 0;
            ps.freeRefreshes = ps.openBar ? 5 : 0;
            ps.rewardState.refreshesThisTurn = 0;   // Teal Tiger Sapphire counts per turn
            ps.rewardState2.refreshesThisTurn = 0;  // the second reward slot counts too
            // Pyramad's +2 Health grows by one for every turn the power was not used.
            if (turn > 1 && ps.power.usesThisTurn == 0 && Power(player) && Power(player)->growsHealthBuff)
                ++ps.power.healthBuffBonus;
            // Hero power per-turn reset and Gallywix's saved gold arrives now.
            ps.power.usesThisTurn = 0;
            ps.power.elementalPlaysThisTurn = 0;
            ps.power.buysThisTurn = 0;
            ps.power.playsThisTurn = 0;
            ps.power.refreshesThisTurn = 0;
            ps.piratesBoughtThisTurn = 0;   // Southsea Strongarm
            if (HeroPowerDef const* const p = Power(player))
            {
                if (p->discountPerTurn > 0 && turn > 1)
                    ps.power.powerCostDiscount += p->discountPerTurn;   // Heistbaron Togwaggle
                if (p->ratKingDiscover)
                {
                    // The Rat King: a new minion type each turn, from this player's lobby.
                    std::vector<uint32_t> const& tribes = PlayerLobbyTribes(player);
                    std::vector<uint32_t> usable;
                    for (uint32_t t : tribes)
                        if (t != 0 && t != kTribeAll)
                            usable.push_back(t);
                    if (usable.empty())
                        for (uint32_t t = 1; t < kTribeAll; t <<= 1)
                            usable.push_back(t);
                    // Only a type with a minion to discover at this Tavern Tier: otherwise the Discover would not open
                    // and the power would cost its Gold for nothing.
                    std::vector<uint32_t> choices;
                    for (uint32_t t : usable)
                        if (m_pool.CountCards(ps.pools, kMinTier, ps.tier, t) > 0)
                            choices.push_back(t);
                    if (choices.empty())
                        choices = usable;
                    if (choices.size() > 1)   // "swaps type each turn": never the same two turns in a row
                        choices.erase(std::remove(choices.begin(), choices.end(), ps.power.ratKingTribe), choices.end());
                    if (!choices.empty())
                        ps.power.ratKingTribe = choices[m_rng.Uniform(static_cast<uint32_t>(choices.size()))];
                }
                if (p->kurtrusPassive && !ps.power.kurtrusDone)
                {
                    ps.power.kurtrusBuys = 0;
                    ps.power.kurtrusUids.clear();
                }
                if (p->cthunEndOfTurn)
                {
                    ps.power.cthunThisTurn = false;
                    if (turn > 1)
                        ++ps.power.cthunRepeats;   // C'Thun: the repeat count grows each turn
                }
            }
            if (ps.power.pendingGold != 0)
            {
                ps.gold += ps.power.pendingGold;
                ps.power.pendingGold = 0;
            }
            // Galewing's flightpath counts down at the start of the turn and pays out when it reaches 0.
            RunPowerCountdown(player);
            // Nguyen's swapped power goes before this turn's effects, so the new choice (queued at the end of
            // StartTurn) is the only power the player has this turn. A flightpath still counting down stays, and no new
            // choice is offered while it does; it was counted first so a 1-turn path still pays.
            if (swapThisTurn && ps.power.powerCountdown <= 0)
            {
                ps.power.powerOverride.clear();
                ps.power.combatPower.clear();
                ps.power.combatPowerOnce = false;
            }
            if (turn > 1 && ps.tier < kMaxTier)
                ps.upgradeCost = std::max(0, ps.upgradeCost - 1);
            // Frozen offers stay and thaw, the others go back to the pool; the tavern is topped up to its size.
            {
                std::vector<Minion> kept;
                for (Minion& o : ps.offers)
                {
                    if ((o.flags & kMinionFrozen) != 0)
                    {
                        o.flags &= ~kMinionFrozen;
                        kept.push_back(o);
                    }
                    else
                        ReturnToPool(player, o);
                }
                ps.offers = kept;
            }
            FillOffers(player);
            Settle(player);
            // Spellcraft minions put their spell in the hand and King Mukla's Bananarama hands out Bananas.
            AddSpellcraftSpells(player);
            if (HeroPowerDef const* const p = Power(player))
                if (p->bananarama)
                {
                    for (int i = 0; i < 2; ++i)
                        AddSpellToHand(player, FindCard("TB_BaconShop_HP_038t"), false, false, false);
                    for (int q = 0; q < Players(); ++q)
                        if (q != player)
                            AddSpellToHand(q, FindCard("TB_BaconShop_HP_038t"), false, false, false);
                }
            FireBoard(player, &EffectDef::onStartOfTurn);
            RewardStartTurn(player);   // Red Hand, The Friends, Yogg, the Alter Ego swap
            Settle(player);
            // Master Nguyen chooses one of 2 new hero powers every turn.
            if (swapThisTurn && ps.power.powerCountdown <= 0)
                OfferPowerChoice(player, PowerChoiceKind::Swap, SwapPool(player), 2);
            // Maiev's held tavern offer comes to the hand after its third StartTurn (+2/+2); Faelin's
            // Tier minions come to the hand as soon as the player's Tier allows. A full hand keeps them for later.
            if (ps.power.holdsOffer)
            {
                if (ps.power.heldTurns > 0)
                    --ps.power.heldTurns;
                if (ps.power.heldTurns <= 0)
                {
                    Minion held = ps.power.heldOffer;
                    held.Buff(2, 2);
                    if (PushToHand(player, held))
                    {
                        ps.power.holdsOffer = false;
                        ps.power.heldOffer = Minion{};
                        ps.power.heldTurns = 0;
                    }
                    // else: keep the buffed minion and try again next turn (do not buff it twice)
                }
            }
            ReleaseExpedition(player);
            if (HeroPowerDef const* const p = Power(player))
            {
                // A. F. Kay / Ambassador Faelin: the first turns pass at once, then their Discovers open.
                if (p->skipTurns > 0 && turn <= p->skipTurns)
                    ps.skipTurn = true;
                else if (p->skipTurns > 0 && turn == p->skipTurns + 1)
                {
                    if (p->skipDiscoverCount > 0 && p->skipDiscoverTier > 0)
                        for (int i = 0; i < p->skipDiscoverCount; ++i)
                            Discover(player, p->skipDiscoverTier, p->skipDiscoverTier, 0);
                    if (p->expeditionTiers)
                        for (int tier = 2; tier <= kMaxTier; tier += 2)
                        {
                            DiscoverRequest req;   // Faelin: one held minion of Tier 2, 4 and 6
                            req.minTier = req.maxTier = tier;
                            req.hold = true;
                            QueueDiscover(player, req);
                        }
                }
                // Tickatus's Prize Wall, at the start of turns 4, 8, 12 and 16. The 4th prize is the
                // last; `prizesTaken` keeps a repeated StartTurn from offering it twice.
                if (p->prizesEvery > 0 && turn >= p->prizesEvery && turn % p->prizesEvery == 0)
                {
                    int const activation = turn / p->prizesEvery;
                    if (activation <= 4 && ps.power.prizesTaken < activation)
                    {
                        ps.power.prizesTaken = activation;
                        DiscoverRequest req;
                        req.prizeTurn = activation;
                        QueueDiscover(player, req);
                    }
                }
            }
        }

        // Faelin's held Tier minions go to the hand once the player's Tavern Tier reaches theirs.
        void Shop::ReleaseExpedition(int player)
        {
            if (!ValidPlayer(player))
                return;
            PlayerShop& ps = State(player);
            for (auto it = ps.power.expedition.begin(); it != ps.power.expedition.end();)
            {
                if (it->Tier() > ps.tier || !PushToHand(player, *it))
                {
                    ++it;
                    continue;
                }
                it = ps.power.expedition.erase(it);
            }
            Settle(player);
        }

        ActionResult Shop::EndTurn(int player)
        {
            ActionScope const scope(*this);   // a reward a quest finishes here turns on at the end
            ActionResult r = Gate(player);
            if (r != ActionResult::Ok)
                return r;
            PlayerShop& ps = State(player);
            FireBoard(player, &EffectDef::onEndOfTurn);
            if (RewardRule const* const d = RewardFor(ps.rewardState))
                if (d->endOfTurnTwice)
                    FireBoard(player, &EffectDef::onEndOfTurn);   // Ghastly Mask doubles your end of turn effects
            if (RewardRule const* const d = RewardFor(ps.rewardState2))
                if (d->endOfTurnTwice)
                    FireBoard(player, &EffectDef::onEndOfTurn);   // a second Ghastly Mask would too
            if (HeroPowerDef const* const p = Power(player))
                if (p->cthunEndOfTurn && ps.power.cthunThisTurn)
                    for (int i = 0; i < 1 + ps.power.cthunRepeats && !ps.board.Empty(); ++i)
                        Buff(player, ps.board[static_cast<int>(m_rng.Uniform(static_cast<uint32_t>(ps.board.Size())))], 1, 1);
            // Sindragosa: Frozen minions in the tavern get +2/+1 at the end of the turn.
            if (HeroPowerDef const* const p = Power(player))
                if (p->stayFrosty)
                {
                    bool any = false;
                    for (Minion& m : ps.offers)
                        if ((m.flags & kMinionFrozen) != 0)
                        {
                            m.Buff(2, 1);
                            any = true;
                        }
                    if (any)
                        RecomputeOfferAuras(player);
                }
            // Sulfuras gives the left- and right-most minions +3/+3 at the end of the turn.
            if (HeroPowerDef const* const p = Power(player))
                if (p->endOfTurnEnds && !ps.board.Empty())
                {
                    Buff(player, ps.board[0], 3, 3);
                    if (ps.board.Size() > 1)
                        Buff(player, ps.board[ps.board.Size() - 1], 3, 3);
                }
            // Gruul Rules gives its host +2/+2 every end of turn, and The Unlimited Coin comes back.
            for (Minion& m : ps.board.Items())
                if (m.gruulRules)
                    Buff(player, m, 2, 2);
            if (ps.unlimitedCoinsOut > 0)
            {
                int const coins = ps.unlimitedCoinsOut;
                ps.unlimitedCoinsOut = 0;
                int const card = FindCard("BGS_Treasures_014");
                for (int i = 0; i < coins; ++i)
                    AddSpellToHand(player, card, false, false, false);
            }
            RewardEndTurn(player);
            Settle(player);
            return ActionResult::Ok;
        }

        void Shop::ReleasePlayer(int player)
        {
            if (!ValidPlayer(player))
                return;
            PlayerShop& ps = State(player);
            ReturnOffers(player);
            for (Minion const& m : ps.options)
                ReturnToPool(player, m);
            ps.options.clear();
            ps.queue.clear();
            for (Minion const& m : ps.hand.Items())
                ReturnToPool(player, m);
            for (Minion const& m : ps.board.Items())
                ReturnToPool(player, m);
            // The minions the hero power holds (Maiev, Faelin) go back too.
            if (ps.power.holdsOffer)
                ReturnToPool(player, ps.power.heldOffer);
            for (Minion const& m : ps.power.expedition)
                ReturnToPool(player, m);
            ps.power.holdsOffer = false;
            ps.power.heldOffer = Minion{};
            ps.power.expedition.clear();
            ps.hand.Clear();
            ps.board.Clear();
            ps.gold = 0;
            ps.powerChoices.clear();
            ps.choiceKind = PowerChoiceKind::None;
        }

        // ------------------------------------------------------------------------------------------ actions

        // The flat 3 plus the hero power and Friends and Family Discount, never below 0.
        int Shop::BuyCost(int player) const
        {
            if (!ValidPlayer(player))
                return kBuyCost;
            PlayerShop const& ps = State(player);
            HeroPowerDef const* const power = Power(player);
            if (power && power->freeBuyAfterAttacks > 0 && ps.power.friendlyAttacks >= power->freeBuyAfterAttacks &&
                ps.power.buysThisTurn == 0)
                return 0;   // Aranna Starseeker: the first minion of the turn is free
            return std::max(0, kBuyCost + (power ? power->buyCostDelta : 0) - ps.tavernCostDiscount);
        }

        ActionResult Shop::Buy(int player, int offerIndex)
        {
            ActionScope const scope(*this);   // a reward a quest finishes here turns on at the end
            ActionResult r = Gate(player);
            if (r != ActionResult::Ok)
                return r;
            PlayerShop& ps = State(player);
            if (offerIndex < 0 || offerIndex >= static_cast<int>(ps.offers.size()))
                return ActionResult::BadIndex;
            HeroPowerDef const* const power = Power(player);
            int const cost = BuyCost(player);
            if (ps.gold < cost)
                return ActionResult::NotEnoughGold;
            Minion const& offer = ps.offers[static_cast<std::size_t>(offerIndex)];
            if (!CanReceive(player, offer.card, offer.golden))
                return ActionResult::HandFull;
            ps.gold -= cost;
            ps.spent += cost;
            FireQuest(player, QuestEventInfo{ QuestEvent::GoldSpent, cost, 0 });
            FireGoldSpent(player, cost);
            Minion m = ps.offers[static_cast<std::size_t>(offerIndex)];
            bool const ticketed = (m.flags & kMinionTicket) != 0;   // Silas Darkmoon
            ps.offers.erase(ps.offers.begin() + offerIndex);
            LeaveTavern(player, m);
            if (power && power->hatOnBuy)
            {
                m.Buff(1, 1);   // Dancin' Deryl: the +1/+1 hat
                ++m.hats;
            }
            PushToHand(player, m);
            ++ps.stats.bought;
            if (HasTribe(CardAt(m.card).tribes, kTribePirate))
                ++ps.piratesBoughtThisTurn;   // Southsea Strongarm / Patches
            {
                int const sc = m_playedCard;
                bool const sg = m_playedGolden;
                m_playedCard = m.card;
                m_playedGolden = m.golden;
                FireOn(player, &EffectDef::onBuy, m.uid);
                FireBoard(player, &EffectDef::onBuy, 0, m.uid);   // Hoggarr: board minions watch the buy too
                m_playedCard = sc;
                m_playedGolden = sg;
            }
            if (HeroPowerDef const* const pw = Power(player))
            {
                bool const pirate = HasTribe(CardAt(m.card).tribes, kTribePirate);
                if (pw->buyOfTribeReducesCost && pirate)
                    ++ps.power.powerCostDiscount;   // Patches the Pirate
                if (pw->brannReward && !ps.power.brannDone)
                {
                    // Dinotamer Brann: every 5 bought Battlecry minions, add Brann Bronzebeard (once per game).
                    EffectDef const* const be = m_effects.Find(m.card);
                    if (be && be->battlecry)
                    {
                        ++ps.power.battlecriesBought;
                        if (pw->brannBattlecries > 0 && ps.power.battlecriesBought >= pw->brannBattlecries)
                        {
                            ps.power.brannDone = true;
                            AddToHand(player, FindCard("BG_LOE_077"), false);
                        }
                    }
                }
                if (pw->kurtrusPassive && !ps.power.kurtrusDone)
                {
                    // Kurtrus Ashfallen: buy N minions in one turn. Rank 0 buffs the bought three, rank 1 the
                    // hand and board, rank 2 all the minions of the game (the ones held now).
                    ps.power.kurtrusUids.push_back(m.uid);
                    ++ps.power.kurtrusBuys;
                    int const need = 3 + ps.power.kurtrusRank;
                    if (ps.power.kurtrusBuys >= need)
                    {
                        int const n = ps.power.kurtrusRank == 2 ? 3 : 2;
                        if (ps.power.kurtrusRank == 0)
                        {
                            for (uint32_t uid : ps.power.kurtrusUids)
                                if (Minion* bought = FindMinion(player, uid))
                                    Buff(player, *bought, n, n);
                        }
                        else
                        {
                            for (Minion& own : ps.board.Items())
                                Buff(player, own, n, n);
                            for (Minion& own : ps.hand.Items())
                                Buff(player, own, n, n);
                        }
                        if (ps.power.kurtrusRank >= 2)
                            ps.power.kurtrusDone = true;
                        else
                            ++ps.power.kurtrusRank;
                        ps.power.kurtrusBuys = 0;
                        ps.power.kurtrusUids.clear();
                    }
                }
            }
            Settle(player);
            ++ps.power.buysThisTurn;
            ++ps.power.buysThisGame;
            // Silas Darkmoon. Buying a ticketed offer gives a ticket; three tickets Discover a minion of
            // the player's Tavern Tier.
            if (ticketed && power && power->ticketsInTavern)
            {
                ++ps.power.tickets;
                if (ps.power.tickets >= 3)
                {
                    ps.power.tickets = 0;
                    Discover(player, ps.tier, ps.tier, 0);
                }
            }
            FireQuest(player, QuestEventInfo{ QuestEvent::Buy });
            RewardBuy(player, m.uid);   // Cooked Book
            return ActionResult::Ok;
        }

        int Shop::SellGold(Minion const& m) const
        {
            if (m.card < 0 || CardAt(m.card).role == CardRole::Spell)
                return 0;   // a spell is cast; a Wild Banana is only thrown away
            if (EffectDef const* const e = m_effects.Find(m.card))
            {
                int const v = m.golden && e->sellGoldGolden != 0 ? e->sellGoldGolden : e->sellGold;
                if (v > 0)
                    return v;   // Freedealing Gambler
            }
            return kSellGold;
        }

        ActionResult Shop::Sell(int player, Zone zone, int index)
        {
            ActionScope const scope(*this);   // a reward a quest finishes here turns on at the end
            ActionResult r = Gate(player);
            if (r != ActionResult::Ok)
                return r;
            PlayerShop& ps = State(player);
            int const size = zone == Zone::Board ? ps.board.Size() : ps.hand.Size();
            if (index < 0 || index >= size)
                return ActionResult::BadIndex;
            uint32_t const uid = zone == Zone::Board ? ps.board[index].uid : ps.hand[index].uid;
            // A spell in the hand is cast, never sold: except a Wild Banana, which needs a Beast and would otherwise
            // clog the hand. It is thrown away for 0 Gold (no sell hooks, no pool copy: it is a token).
            if (CardAt(zone == Zone::Board ? ps.board[index].card : ps.hand[index].card).role == CardRole::Spell)
            {
                if (zone != Zone::Hand || std::strcmp(CardAt(ps.hand[index].card).id, "TB_BaconShop_HP_038t") != 0)
                    return ActionResult::BadIndex;
                ps.hand.Remove(index);
                return ActionResult::Ok;
            }
            FireOn(player, &EffectDef::onSell, uid);
            RewardSell(player, uid);   // Anima Bribe, while the sold minion is still here
            int pos = zone == Zone::Board ? ps.board.IndexOfUid(uid) : ps.hand.IndexOfUid(uid);
            if (pos < 0)
                return ActionResult::Ok; // the sell hook already removed it
            Minion sold = zone == Zone::Board ? ps.board.Remove(pos) : ps.hand.Remove(pos);
            AddGold(player, SellGold(sold));
            ReturnToPool(player, sold);
            ++ps.stats.sold;
            ++ps.power.minionsSoldThisGame;   // Ozumat: the Tentacle's +1/+1
            Settle(player);
            if (HeroPowerDef const* const p = Power(player))
            {
                if (p->goldOnSellNextTurn != 0)
                    ps.power.pendingGold += p->goldOnSellNextTurn;   // Gallywix, paid at the next StartTurn
                if (p->murlocPerSells > 0)
                {
                    ++ps.power.sellsThisGame;   // Fungalmancer Flurgl: every N sells, a random Murloc
                    if (ps.power.sellsThisGame % p->murlocPerSells == 0)
                        AddRandomToHand(player, kMinTier, ps.tier, kTribeMurloc);
                }
                if (p->hatOnBuy && sold.hats > 0 && !ps.board.Empty())
                {
                    // Deryl: the sold minion's hats pass to a random friendly minion, each still +1/+1.
                    Minion& to = ps.board[static_cast<int>(m_rng.Uniform(static_cast<uint32_t>(ps.board.Size())))];
                    to.hats += sold.hats;
                    Buff(player, to, sold.hats, sold.hats);
                }
            }
            return ActionResult::Ok;
        }

        ActionResult Shop::Play(int player, int handIndex, int slot, uint32_t targetUid)
        {
            ActionScope const scope(*this);   // a reward a quest finishes here turns on at the end
            ActionResult r = Gate(player);
            if (r != ActionResult::Ok)
                return r;
            PlayerShop& ps = State(player);
            if (handIndex < 0 || handIndex >= ps.hand.Size())
                return ActionResult::BadIndex;
            if (CardAt(ps.hand[handIndex].card).role == CardRole::Spell)
                return ActionResult::BadIndex;   // spells are cast, not played
            bool const magnetic = (ps.hand[handIndex].keywords & kKwMagnetic) != 0;
            // Played directly to the left of a Mech, a Magnetic minion merges into the Mech on its right.
            if (magnetic && targetUid == 0 && slot >= 0 && slot < ps.board.Size() && ps.board[slot].IsTribe(kTribeMech))
                targetUid = ps.board[slot].uid;
            int const targetIdx = targetUid != 0 ? ps.board.IndexOfUid(targetUid) : -1;
            if (targetUid != 0 && targetIdx < 0)
                return ActionResult::BadTarget;
            if (magnetic && targetUid != 0 && !ps.board[targetIdx].IsTribe(kTribeMech))
                return ActionResult::BadTarget;
            if (ps.board.Full() && !(magnetic && targetUid != 0))
                return ActionResult::BoardFull; // a Magnetic merge takes no board slot

            int const rewardTier = std::min(ps.tier + 1, kMaxTier);
            Minion m = ps.hand.Remove(handIndex);

            if (magnetic && targetUid != 0)
            {
                Minion& t = ps.board[targetIdx];
                Attachment a;
                a.card = m.card;
                a.golden = m.golden;
                a.attack = m.attack;
                a.health = m.health;
                a.keywords = m.keywords & ~static_cast<uint32_t>(kKwMagnetic);
                a.poolCopies = m.poolCopies;
                t.Buff(a.attack, a.health);
                t.keywords |= a.keywords;
                t.attachments.push_back(a);
                for (Attachment const& inner : m.attachments)
                    t.attachments.push_back(inner);
                RecomputeBoard(player);
                {
                    int const sc = m_playedCard;
                    bool const sg = m_playedGolden;
                    bool const sm = m_playedMagnetic;
                    m_playedCard = m.card;
                    m_playedGolden = m.golden;
                    m_playedMagnetic = true;
                    FireBoard(player, &EffectDef::onFriendlyPlayed, 0, targetUid);
                    m_playedCard = sc;
                    m_playedGolden = sg;
                    m_playedMagnetic = sm;
                }
                if (m.golden)
                {
                    DiscoverRequest req;
                    req.minTier = req.maxTier = rewardTier;
                    QueueDiscover(player, req);
                }
                AfterPlay(player, targetUid, m.card);   // Kael'thas counts the merged play on the host, before a triple can merge it
                Settle(player);
                FireQuest(player, QuestEventInfo{ QuestEvent::PlayTribe, 1, CardAt(m.card).tribes });  // a Magnetic attach is a play
                return ActionResult::Ok;
            }

            ps.board.Insert(slot, m);
            CountPirateSummoned(player, m.card);   // Salty Looter counts a Pirate played from the hand
            RecomputeBoard(player);
            {
                int const times = BattlecryTimes(player);
                for (int i = 0; i < times; ++i)
                    RunBattlecry(player, m.uid, targetUid);
            }
            {
                int const sc = m_playedCard;
                bool const sg = m_playedGolden;
                bool const sm = m_playedMagnetic;
                m_playedCard = m.card;
                m_playedGolden = m.golden;
                m_playedMagnetic = false;
                FireBoard(player, &EffectDef::onFriendlyPlayed, m.uid, m.uid);
                // "After you play an Elemental" fires on the played Elemental itself too (FireBoard skips the
                // played minion). Only Elemental plays, so the other cards' self-exclusion is unchanged.
                if (HasTribe(CardAt(m.card).tribes, kTribeElemental))
                    FireOn(player, &EffectDef::onFriendlyPlayed, m.uid, m.uid, m.uid);
                FireBoard(player, &EffectDef::onFriendlySummonedShop, m.uid, m.uid);
                HeroSummoned(player, m.uid);
                m_playedCard = sc;
                m_playedGolden = sg;
                m_playedMagnetic = sm;
            }
            if (m.golden)
            {
                DiscoverRequest req;
                req.minTier = req.maxTier = rewardTier;
                QueueDiscover(player, req);
            }
            for (int t = 0; t < kWeeklyTribes; ++t)
                if (HasTribe(CardAt(m.card).tribes, 1u << t))
                    ++ps.stats.played[t];
            AfterPlay(player, m.uid, m.card);   // before Settle: a triple completed by this play replaces the minion's uid
            Settle(player);
            FireQuest(player, QuestEventInfo{ QuestEvent::Summon });           // playing a minion counts as summoning it
            FireQuest(player, QuestEventInfo{ QuestEvent::PlayTribe, 1, CardAt(m.card).tribes });
            if (EffectDef const* const e = m_effects.Find(m.card))
                if (e->battlecry)
                    FireQuest(player, QuestEventInfo{ QuestEvent::PlayBattlecry });
            return ActionResult::Ok;
        }

        // Cast a spell from the hand. A spell that needs a minion is refused without a valid friendly
        // target (the tribe rule is Wild Banana's Beast). The Spellcraft permanence (Vashj, Lava Lurker) is decided
        // before the effect so its buffs use the permanent path.
        ActionResult Shop::Cast(int player, int handIndex, uint32_t targetUid)
        {
            ActionScope const scope(*this);
            ActionResult r = Gate(player);
            if (r != ActionResult::Ok)
                return r;
            PlayerShop& ps = State(player);
            if (handIndex < 0 || handIndex >= ps.hand.Size())
                return ActionResult::BadIndex;
            Minion const spell = ps.hand[handIndex];
            if (CardAt(spell.card).role != CardRole::Spell)
                return ActionResult::BadIndex;
            EffectDef const* const e = m_effects.Find(spell.card);
            if (!e || !e->onCast)
                return ActionResult::BadIndex;
            int const targetIdx = targetUid != 0 ? ps.board.IndexOfUid(targetUid) : -1;
            if (targetUid != 0 && targetIdx < 0)
                return ActionResult::BadTarget;
            Minion* target = targetIdx >= 0 ? &ps.board[targetIdx] : nullptr;
            if (e->spellNeedsTarget)
            {
                if (!target)
                    return ActionResult::BadTarget;
                if (e->spellTargetTribe != 0 && !target->IsTribe(e->spellTargetTribe))
                    return ActionResult::BadTarget;
                if (e->spellTargetNotGolden && target->golden)
                    return ActionResult::BadTarget;   // Repeat Customer / Raise the Stakes
            }
            ps.hand.Remove(handIndex);
            ++ps.stats.spells;
            bool permanent = spell.permanentSpell;
            if (target && e->isSpellcraftSpell)
            {
                // Lava Lurker: the first (golden: two) Spellcraft spells on it each turn are permanent.
                EffectDef const* const te = m_effects.Find(target->card);
                if (te && te->spellcraftPermanentPerTurn > 0)
                {
                    int const allowed = target->golden && te->spellcraftPermanentPerTurnGolden > 0
                        ? te->spellcraftPermanentPerTurnGolden : te->spellcraftPermanentPerTurn;
                    if (target->counters[kCounterSpellcraftPermanent] < allowed)
                    {
                        ++target->counters[kCounterSpellcraftPermanent];
                        permanent = true;
                    }
                }
            }
            ShopCtx ctx(*this, player, spell.golden);
            ctx.target = target;
            ctx.playedCard = spell.card;
            ctx.playedGolden = spell.golden;
            ctx.spellCard = spell.card;
            ctx.spellcraft = e->isSpellcraftSpell;
            ctx.spellPermanent = permanent;
            if (spell.card == FindCard("BG20_GEM") && target)
                PlayBloodGem(player, *target);   // the gem's value and its on-target triggers
            else
                e->onCast(ctx, spell.golden);
            FireSpellCast(player, spell.card, spell.golden, e->isSpellcraftSpell, targetUid);
            Settle(player);
            return ActionResult::Ok;
        }

        ActionResult Shop::Send(int player, int handIndex, int ally)
        {
            ActionScope const scope(*this);   // a reward a quest finishes here turns on at the end
            ActionResult r = Gate(player);
            if (r != ActionResult::Ok)
                return r;
            if (!ValidPlayer(ally) || ally == player)
                return ActionResult::NotAlly;
            PlayerShop& ps = State(player);
            if (ps.sentThisTurn)
                return ActionResult::SendUsed;
            if (handIndex < 0 || handIndex >= ps.hand.Size())
                return ActionResult::BadIndex;
            if (CardAt(ps.hand[handIndex].card).role == CardRole::Spell)
                return ActionResult::BadIndex;   // a spell is not sent to an ally
            Minion const m = ps.hand[handIndex];
            if (!CanReceive(ally, m.card, m.golden))
                return ActionResult::HandFull;
            ps.hand.Remove(handIndex);
            PushToHand(ally, m);
            ps.sentThisTurn = true;
            {
                std::vector<int>& gifts = State(ally).gifts;   // note for the receiver's window
                gifts.push_back(m.card);
                if (gifts.size() > 8)
                    gifts.erase(gifts.begin());   // NPC seats are never drained: keep it bounded
            }
            Settle(ally);          // a sent copy can complete a triple at once
            Settle(player);
            return ActionResult::Ok;
        }

        ActionResult Shop::HeroPower(int player, uint32_t targetUid, int offerIndex, uint32_t secondUid)
        {
            ActionScope const scope(*this);   // a reward a quest finishes here turns on at the end
            ActionResult r = Gate(player);
            if (r != ActionResult::Ok)
                return r;
            PlayerShop& ps = State(player);
            HeroPowerDef const* const p = Power(player);
            if (!p || !p->activate)
                return ActionResult::NoPower;
            if (p->oncePerGame && ps.power.Uses(p->id) > 0)
                return ActionResult::PowerUsed;
            if (p->usesPerGame > 0 && ps.power.Uses(p->id) >= p->usesPerGame)
                return ActionResult::PowerUsed;
            if (p->usesPerTurn > 0 ? ps.power.usesThisTurn >= p->usesPerTurn
                                   : (p->oncePerTurn && ps.power.usesThisTurn > 0))
                return ActionResult::PowerUsed;
            if (p->armsCombat && ps.power.combatArmed)
                return ActionResult::PowerUsed;   // armed already (a resting player keeps it for the next fight)
            if (p->target == PowerTarget::FriendlyMinion && (targetUid == 0 || ps.board.IndexOfUid(targetUid) < 0))
                return ActionResult::BadTarget;
            if (p->target == PowerTarget::TavernOffer)
            {
                if (offerIndex < 0 || offerIndex >= static_cast<int>(ps.offers.size()))
                    return ActionResult::BadTarget;
                if (p->holdsOffer && ps.power.holdsOffer)
                    return ActionResult::PowerUsed;   // Maiev: one held minion at a time
                if (!p->stayFrosty && !p->holdsOffer && !CanReceive(player, ps.offers[static_cast<std::size_t>(offerIndex)].card, false))
                    return ActionResult::HandFull;   // Sindragosa only freezes the offer, Maiev only holds it
            }
            if (p->target == PowerTarget::Seat && (offerIndex < 0 || offerIndex >= Players()))
                return ActionResult::BadTarget;
            if (p->target == PowerTarget::TwoMinions)
            {
                auto const known = [&](uint32_t uid)
                {
                    if (uid == 0)
                        return false;
                    if (ps.board.IndexOfUid(uid) >= 0)
                        return true;
                    for (Minion const& o : ps.offers)
                        if (o.uid == uid)
                            return true;
                    return false;
                };
                if (targetUid == secondUid || !known(targetUid) || !known(secondUid))
                    return ActionResult::BadTarget;
            }
            if (p->targetOk && !p->targetOk(*this, player, targetUid))
                return ActionResult::BadTarget;   // a power that would do nothing is refused before it is paid
            int const cost = HeroPowerCost(*p, ps.power);
            if (ps.gold < cost)
                return ActionResult::NotEnoughGold;
            ps.gold -= cost;
            ps.spent += cost;
            FireQuest(player, QuestEventInfo{ QuestEvent::GoldSpent, cost, 0 });
            FireGoldSpent(player, cost);
            ++ps.power.usesThisTurn;
            ++ps.power.usesThisGame;
            ps.power.AddUse(p->id);
            ps.power.secondTarget = secondUid;
            p->activate(*this, player, ps.power, targetUid, offerIndex);
            if (p->buyOfTribeReducesCost)
                ps.power.powerCostDiscount = 0;   // Patches: the discount pays for this use
            if (ps.rewardState.powerExtraTrigger || ps.rewardState2.powerExtraTrigger)
                p->activate(*this, player, ps.power, targetUid, offerIndex);   // Wheel: Mysterybox
            Settle(player);
            return ActionResult::Ok;
        }

        bool Shop::MakeGolden(int player, uint32_t uid)
        {
            if (!ValidPlayer(player))
                return false;
            PlayerShop& ps = State(player);
            int const i = ps.board.IndexOfUid(uid);
            if (i < 0)
                return false;
            Minion& m = ps.board[static_cast<std::size_t>(i)];
            if (m.golden || m.card < 0 || CardAt(m.card).role == CardRole::Spell || CardAt(m.card).goldenId[0] == '\0')
                return false;
            CardDef const& d = CardAt(m.card);
            int const da = d.goldenAttack - d.attack;
            int const dh = d.goldenHealth - d.health;
            m.attack += da;
            m.health += dh;
            m.maxHealth += dh;
            uint32_t extra = d.goldenKeywords & ~d.keywords;
            if (extra & kKwMegaWindfury)
                m.keywords &= ~static_cast<uint32_t>(kKwWindfury);
            m.keywords |= extra;
            m.golden = true;
            RecomputeBoard(player);
            Settle(player);   // no Triple Reward (Reno Jackson)
            return true;
        }

        void Shop::TakeDamage(int player, int amount)
        {
            if (!ValidPlayer(player) || amount <= 0)
                return;
            PlayerShop& ps = State(player);
            int dmg = amount;
            if (ps.armor > 0)
            {
                int const a = std::min(ps.armor, dmg);
                ps.armor -= a;
                dmg -= a;
            }
            ps.health -= dmg;
        }

        ActionResult Shop::Move(int player, int fromSlot, int toSlot)
        {
            ActionScope const scope(*this);   // a reward a quest finishes here turns on at the end
            ActionResult r = Gate(player);
            if (r != ActionResult::Ok)
                return r;
            PlayerShop& ps = State(player);
            if (fromSlot < 0 || fromSlot >= ps.board.Size() || toSlot < 0 || toSlot >= ps.board.Size())
                return ActionResult::BadIndex;
            if (fromSlot != toSlot)
            {
                Minion m = ps.board.Remove(fromSlot);
                ps.board.Insert(toSlot, m);
                RecomputeBoard(player);
            }
            return ActionResult::Ok;
        }

        int Shop::RefreshCost(int player) const
        {
            if (!ValidPlayer(player))
                return kRefreshCost;
            PlayerShop const& ps = State(player);
            HeroPowerDef const* const p = Power(player);
            int cost = kRefreshCost + (p ? p->refreshCostDelta : 0);
            if (!ps.refreshDiscounts.empty())
                cost -= ps.refreshDiscounts.front();   // Recycling Wraith / Refreshing Anomaly
            if (p && p->firstRefreshFree && ps.power.refreshesThisTurn == 0)
                cost = 0;                     // Nozdormu: the first Refresh each turn is free
            if (ps.openBar && ps.freeRefreshes > 0)
                cost = 0;                     // Open Bar's first five refreshes of the turn
            return std::max(0, cost);
        }

        ActionResult Shop::Refresh(int player)
        {
            ActionScope const scope(*this);   // a reward a quest finishes here turns on at the end
            ActionResult r = Gate(player);
            if (r != ActionResult::Ok)
                return r;
            PlayerShop& ps = State(player);
            int const cost = RefreshCost(player);
            if (ps.gold < cost)
                return ActionResult::NotEnoughGold;
            ps.gold -= cost;
            ps.spent += cost;
            FireQuest(player, QuestEventInfo{ QuestEvent::GoldSpent, cost, 0 });
            FireGoldSpent(player, cost);
            ++ps.power.refreshesThisTurn;
            ++ps.power.refreshesThisGame;
            if (ps.openBar && ps.freeRefreshes > 0)
                --ps.freeRefreshes;   // Open Bar's free Refresh; it leaves the refresh discounts unspent
            else if (!ps.refreshDiscounts.empty())
                ps.refreshDiscounts.erase(ps.refreshDiscounts.begin());
            RefreshOffers(player);
            return ActionResult::Ok;
        }

        void Shop::RefreshOffers(int player)
        {
            if (!ValidPlayer(player))
                return;
            PlayerShop& ps = State(player);
            ++ps.rewardState.refreshesThisTurn;   // Teal Tiger Sapphire
            ++ps.rewardState2.refreshesThisTurn;
            ReturnOffers(player);
            FillOffers(player);
            // Varden Dawngrasp: after a Refresh, copy the highest-Tier offer and freeze them both.
            if (HeroPowerDef const* const p = Power(player))
                if (p->twiceAsNice && !ps.offers.empty())
                {
                    int best = -1;
                    std::vector<int> top;
                    for (int i = 0; i < static_cast<int>(ps.offers.size()); ++i)
                    {
                        int const tier = ps.offers[static_cast<std::size_t>(i)].Tier();
                        if (tier > best)
                        {
                            best = tier;
                            top.clear();
                        }
                        if (tier == best)
                            top.push_back(i);
                    }
                    if (!top.empty())
                    {
                        Minion& original = ps.offers[static_cast<std::size_t>(top[m_rng.Uniform(static_cast<uint32_t>(top.size()))])];
                        original.flags |= kMinionFrozen;
                        Minion copy = original;
                        copy.uid = NewUid();
                        copy.flags = kMinionFrozen;
                        copy.poolCopies = 0;
                        copy.attachments.clear();   // the attachments' pool copies belong to the original
                        ps.offers.push_back(copy);
                    }
                }
            FireQuest(player, QuestEventInfo{ QuestEvent::Refresh });   // manual and free refreshes count, the restock does not
            FireBoard(player, &EffectDef::onRefresh);
            Settle(player);
            RewardRefresh(player);   // Mirror Shield, Wondrous Wisdomball
        }

        void Shop::DrawHigherTierOffer(int player)
        {
            if (!ValidPlayer(player))
                return;
            PlayerShop& ps = State(player);
            if (ps.tier >= kMaxTier || ps.offers.empty())
                return;
            std::vector<std::size_t> slots;   // offers not yet from the higher tier (Toki draws two)
            for (std::size_t i = 0; i < ps.offers.size(); ++i)
                if (ps.offers[i].Tier() <= ps.tier)
                    slots.push_back(i);
            if (slots.empty())
                return;
            int const card = m_pool.Draw(m_rng, ps.pools, ps.tier + 1, ps.tier + 1);
            if (card < 0)
                return;
            std::size_t const idx = slots[m_rng.Uniform(static_cast<uint32_t>(slots.size()))];
            ReturnToPool(player, ps.offers[idx]);
            ps.offers[idx] = MakeCopy(card, false, 1);   // the new minion is not Frozen
            RecomputeOfferAuras(player);
        }

        ActionResult Shop::Freeze(int player, bool frozen)
        {
            ActionScope const scope(*this);   // a reward a quest finishes here turns on at the end
            ActionResult r = Gate(player);
            if (r != ActionResult::Ok)
                return r;
            for (Minion& o : State(player).offers)
                o.flags = frozen ? (o.flags | kMinionFrozen) : (o.flags & ~kMinionFrozen);
            return ActionResult::Ok;
        }

        bool Shop::OffersFrozen(int player) const
        {
            if (!ValidPlayer(player) || State(player).offers.empty())
                return false;
            for (Minion const& o : State(player).offers)
                if ((o.flags & kMinionFrozen) == 0)
                    return false;
            return true;
        }

        bool Shop::AnyOfferFrozen(int player) const
        {
            if (!ValidPlayer(player))
                return false;
            for (Minion const& o : State(player).offers)
                if ((o.flags & kMinionFrozen) != 0)
                    return true;
            return false;
        }

        void Shop::FreezeOffer(int player, int index)
        {
            if (!ValidPlayer(player))
                return;
            PlayerShop& ps = State(player);
            if (index < 0)
                index = static_cast<int>(ps.offers.size()) - 1;
            if (index >= 0 && index < static_cast<int>(ps.offers.size()))
                ps.offers[static_cast<std::size_t>(index)].flags |= kMinionFrozen;
        }

        ActionResult Shop::Upgrade(int player)
        {
            ActionScope const scope(*this);   // a reward a quest finishes here turns on at the end
            ActionResult r = Gate(player);
            if (r != ActionResult::Ok)
                return r;
            PlayerShop& ps = State(player);
            if (ps.tier >= kMaxTier)
                return ActionResult::MaxTier;
            int const cost = UpgradeCost(player);
            if (ps.gold < cost)
                return ActionResult::NotEnoughGold;
            ps.gold -= cost;
            ps.spent += cost;
            FireQuest(player, QuestEventInfo{ QuestEvent::GoldSpent, cost, 0 });
            FireGoldSpent(player, cost);
            ++ps.tier;
            ps.upgradeCost = ps.tier >= kMaxTier ? 0 : UpgradeBaseCost(ps.tier + 1);
            FireBoard(player, &EffectDef::onTavernUpgrade);
            Settle(player);
            if (HeroPowerDef const* const p = Power(player))
            {
                if (p->goldOnUpgrade != 0)
                    AddGold(player, p->goldOnUpgrade);   // Forest Warden Omu: +2 Gold this turn
                if (p->discoverTribeAtTier > 0 && ps.tier == p->discoverTribeAtTier)
                    for (int i = 0; i < p->discoverCount; ++i)
                        Discover(player, kMinTier, kMaxTier, p->discoverTribe);   // Alexstrasza
            }
            ReleaseExpedition(player);   // Faelin's minions whose Tier was just reached
            if (HeroPowerDef const* const p = Power(player))
                if (p->stanceChoice)
                    OfferPowerChoice(player, PowerChoiceKind::Stance, { kStanceUnited, kStanceOffense, kStanceDefense }, 3);   // Cariel
            return ActionResult::Ok;
        }

        // ------------------------------------------------------------------------------------------ ShopApi

        void Shop::AddGold(int player, int amount)
        {
            PlayerShop& ps = State(player);
            ps.gold = std::max(0, ps.gold + amount);
        }

        Minion* Shop::FindMinion(int player, uint32_t uid)
        {
            if (!ValidPlayer(player) || uid == 0)
                return nullptr;
            PlayerShop& ps = State(player);
            int i = ps.board.IndexOfUid(uid);
            if (i >= 0)
                return &ps.board[i];
            i = ps.hand.IndexOfUid(uid);
            if (i >= 0)
                return &ps.hand[i];
            for (Minion& m : ps.offers)
                if (m.uid == uid)
                    return &m;
            for (Minion& m : ps.options)
                if (m.uid == uid)
                    return &m;
            return nullptr;
        }

        void Shop::Buff(int player, Minion& m, int attack, int health)
        {
            uint32_t const uid = m.uid;
            m.Buff(attack, health);
            RecomputeBoard(player);
            // "increase a friendly minion's stats" counts a board or hand minion only, once per buff instance
            // (an aura is recomputed, not applied, so it never gets here; a tavern offer is not friendly).
            if (attack > 0 || health > 0)   // a debuff is not an increase
            {
                PlayerShop const& ps = State(player);
                if (ps.board.IndexOfUid(uid) >= 0 || ps.hand.IndexOfUid(uid) >= 0)
                    FireQuest(player, QuestEventInfo{ QuestEvent::StatsIncreased });
            }
            if (attack > 0 && State(player).board.IndexOfUid(uid) >= 0)
            {
                int const sa = m_buffAttack;
                int const sh = m_buffHealth;
                m_buffAttack = attack;
                m_buffHealth = health;
                FireBoard(player, &EffectDef::onFriendlyBuffedShop, uid, uid);
                m_buffAttack = sa;
                m_buffHealth = sh;
            }
        }

        void Shop::GiveKeywords(int player, Minion& m, uint32_t keywords)
        {
            m.keywords |= keywords;
            RecomputeBoard(player);
        }

        // A spell's stats are recorded as temporary on the minion (taken back at the owner's next
        // StartTurn) unless the spell's effect is permanent (Vashj, Lava Lurker).
        void Shop::SpellBuff(int player, Minion& m, int attack, int health, bool permanent)
        {
            uint32_t const uid = m.uid;
            Buff(player, m, attack, health);
            if (!permanent)
                if (Minion* after = FindMinion(player, uid))
                {
                    after->tempAttack += attack;
                    after->tempHealth += health;
                }
        }

        void Shop::SpellKeywords(int player, Minion& m, uint32_t keywords, bool permanent)
        {
            uint32_t const uid = m.uid;
            uint32_t const add = keywords & ~m.Keywords();
            if (add == 0)
                return;
            GiveKeywords(player, m, add);
            if (!permanent)
                if (Minion* after = FindMinion(player, uid))
                    after->tempKeywords |= add;
        }

        uint32_t Shop::AddSpellToHand(int player, int card, bool golden, bool temporary, bool permanentEffect)
        {
            if (!ValidPlayer(player) || card < 0 || card >= CardCount() || CardAt(card).role != CardRole::Spell)
                return 0;
            Minion m = MakeCopy(card, golden, 0);
            m.temporary = temporary;
            m.permanentSpell = permanentEffect;
            if (!PushToHand(player, m))
                return 0;
            Settle(player);
            return m.uid;
        }

        bool Shop::DiscoverSpell(int player, int maxTier, bool permanentEffect)
        {
            if (!ValidPlayer(player) || SpellcraftSpells(maxTier).empty())
                return false;
            DiscoverRequest req;
            req.minTier = 1;
            req.maxTier = maxTier;
            req.spell = true;
            req.permanentSpell = permanentEffect;
            QueueDiscover(player, req);
            return true;
        }

        // The Great Akazamzarak. Up to three Secrets the player does not have yet; the chosen one goes
        // into play. False when the cap is reached or every Secret is already in play.
        bool Shop::DiscoverSecret(int player)
        {
            if (!ValidPlayer(player))
                return false;
            PlayerShop const& ps = State(player);
            if (static_cast<int>(ps.secrets.size()) >= 5)
                return false;
            bool available = false;
            for (int c : SecretCardIds())
                if (std::find(ps.secrets.begin(), ps.secrets.end(), c) == ps.secrets.end())
                {
                    available = true;
                    break;
                }
            if (!available)
                return false;
            DiscoverRequest req;
            req.secret = true;
            QueueDiscover(player, req);
            return true;
        }

        // Ice Block. The damage of the round is prevented when it would kill the hero; the secret then
        // leaves play (the hero is Immune for the rest of the round: there is one damage step per round).
        bool Shop::PreventFatalDamage(int player, int amount)
        {
            if (!ValidPlayer(player) || amount <= 0)
                return false;
            PlayerShop& ps = State(player);
            int const iceBlock = FindCard("TB_Bacon_Secrets_12");
            if (iceBlock < 0)
                return false;
            auto const it = std::find(ps.secrets.begin(), ps.secrets.end(), iceBlock);
            if (it == ps.secrets.end())
                return false;
            int const toHealth = std::max(0, amount - std::min(ps.armor, amount));
            if (ps.health - toHealth > 0)
                return false;   // the hit is not fatal: Ice Block stays
            ps.secrets.erase(it);
            return true;
        }

        // A Blood Gem on a friendly minion. Its own onCast gives the stats (Agamaggan included), then
        // onBloodGem fires on that minion and onBloodGemPlayed on the rest of the board. Used by Cast and by the
        // minions that play a gem without the hand (Bannerboar, Charlga, Necrolyte, Darkgaze Elder).
        void Shop::PlayBloodGem(int player, Minion& target)
        {
            if (!ValidPlayer(player))
                return;
            int const gem = FindCard("BG20_GEM");
            EffectDef const* const e = gem >= 0 ? m_effects.Find(gem) : nullptr;
            if (!e || !e->onCast)
                return;
            uint32_t const uid = target.uid;
            ShopCtx ctx(*this, player, false);
            ctx.target = &target;
            ctx.playedCard = gem;
            ctx.spellCard = gem;
            e->onCast(ctx, false);
            if (!FindMinion(player, uid))
                return;
            FireOn(player, &EffectDef::onBloodGem, uid);
            FireBoard(player, &EffectDef::onBloodGemPlayed, 0, uid);
        }

        // "until next turn" stats and keywords end at the owner's StartTurn; a Spellcraft card left in
        // the hand leaves it too. The per-turn mark of Snail Cavalry / Lava Lurker resets here.
        void Shop::ClearTemporary(int player)
        {
            if (!ValidPlayer(player))
                return;
            PlayerShop& ps = State(player);
            auto const clear = [](Minion& m)
            {
                if (m.tempAttack != 0 || m.tempHealth != 0)
                {
                    m.attack -= m.tempAttack;
                    m.maxHealth -= m.tempHealth;
                    m.health -= m.tempHealth;
                    if (m.health > m.maxHealth)
                        m.health = m.maxHealth;
                    if (m.health < 1)
                        m.health = 1;
                    m.tempAttack = 0;
                    m.tempHealth = 0;
                }
                if (m.tempKeywords != 0)
                {
                    m.keywords &= ~m.tempKeywords;
                    m.tempKeywords = 0;
                }
                m.counters[kCounterOncePerTurn] = 0;
            };
            for (Minion& m : ps.board.Items())
                clear(m);
            for (Minion& m : ps.hand.Items())
                clear(m);
            for (int i = ps.hand.Size() - 1; i >= 0; --i)
                if (ps.hand[i].temporary)
                    ps.hand.Remove(i);
        }

        // Spellcraft: every friendly minion with the keyword puts its spell in the hand at the owner's StartTurn.
        void Shop::AddSpellcraftSpells(int player)
        {
            if (!ValidPlayer(player))
                return;
            std::vector<Minion> casters = State(player).board.Items();
            for (Minion const& m : casters)
            {
                EffectDef const* const e = m_effects.Find(m.card);
                if (!e || !e->grantsSpellcraft)
                    continue;
                int const card = FindCard(e->grantsSpellcraft);
                if (card >= 0)
                    AddSpellToHand(player, card, m.golden, true, false);
            }
        }

        // Queen Azshara: when the warband reaches 25 total Attack, Naga Conquest replaces her passive.
        void Shop::CheckNagaConquest(int player)
        {
            if (!ValidPlayer(player))
                return;
            PlayerShop& ps = State(player);
            if (ps.power.nagaConquest || ps.hero < 0 || ps.hero >= HeroCount())
                return;
            HeroPowerDef const* const base = m_heroPowers.Find(HeroAt(ps.hero).powerId);
            if (!base || !base->conquestPowerId)
                return;
            int total = 0;
            for (Minion const& m : ps.board.Items())
                total += std::max(0, m.attack);
            if (total >= 25)
            {
                // The conquest is a power override, like the other power swaps.
                ps.power.nagaConquest = true;
                if (HeroPowerDef const* const conquest = m_heroPowers.Find(base->conquestPowerId))
                    ps.power.powerOverride = conquest->id;
            }
        }

        // Galewing's flightpath. It counts down at the owner's StartTurn; the turn it reaches 0 it pays
        // its bonus and the power goes back to Dungar's Gryphon. Westfall buffs the left-most minion, Ironforge gives
        // Gold and Eastern Plaguelands discovers a minion of the Tavern Tier (the render values).
        void Shop::RunPowerCountdown(int player)
        {
            if (!ValidPlayer(player))
                return;
            PlayerShop& ps = State(player);
            if (ps.power.powerOverride.empty())
                return;
            HeroPowerDef const* const def = m_heroPowers.Find(ps.power.powerOverride);
            if (!def || def->countdownTurns <= 0)
                return;
            if (ps.power.powerCountdown <= 0)
                ps.power.powerCountdown = def->countdownTurns;
            if (--ps.power.powerCountdown > 0)
                return;
            if (def->countdownGold != 0)
                ps.gold += def->countdownGold;
            if ((def->countdownAttack != 0 || def->countdownHealth != 0) && !ps.board.Empty())
                Buff(player, ps.board[0], def->countdownAttack, def->countdownHealth);
            if (def->countdownDiscover)
                Discover(player, ps.tier, ps.tier, 0);
            ps.power.powerOverride.clear();
            ps.power.powerCountdown = 0;
        }

        void Shop::SetLastCombatWon(int player, bool won)
        {
            if (!ValidPlayer(player))
                return;
            State(player).lostLastCombat = !won;
            // Lord Barov's guess is settled by the guessed player's combat; a guess on a player who rested is
            // cleared by ClearBarovGuesses at the end of the round.
            for (int p = 0; p < Players(); ++p)
            {
                PlayerShop& ps = State(p);
                if (ps.power.barovGuess != player)
                    continue;
                ps.power.barovGuess = -1;
                if (won)
                    for (int i = 0; i < 3; ++i)
                        AddSpellToHand(p, FindCard("TB_BaconShop_HP_008a"), false, false, false);
            }
        }

        void Shop::ClearBarovGuesses()
        {
            for (PlayerShop& ps : m_players)
                ps.power.barovGuess = -1;
        }

        uint32_t Shop::SummonToBoard(int player, int card, bool golden, int position)
        {
            if (!ValidPlayer(player) || card < 0 || card >= CardCount())
                return 0;
            PlayerShop& ps = State(player);
            if (ps.board.Full())
                return 0;
            Minion m = MakeCopy(card, golden, 0);
            ps.board.Insert(position, m);
            CountPirateSummoned(player, card);   // Salty Looter counts a summoned Pirate
            RecomputeBoard(player);
            {
                int const sc = m_playedCard;
                bool const sg = m_playedGolden;
                bool const sm = m_playedMagnetic;
                m_playedCard = m.card;      // a summoned minion is its own "played" card for the summon hook
                m_playedGolden = m.golden;
                m_playedMagnetic = false;
                FireBoard(player, &EffectDef::onFriendlySummonedShop, m.uid, m.uid);
                HeroSummoned(player, m.uid);
                m_playedCard = sc;
                m_playedGolden = sg;
                m_playedMagnetic = sm;
            }
            Settle(player);
            FireQuest(player, QuestEventInfo{ QuestEvent::Summon });   // a shop-phase summon feeds Assemble a Lineup
            return m.uid;
        }

        uint32_t Shop::AddToHand(int player, int card, bool golden)
        {
            if (!ValidPlayer(player) || card < 0 || card >= CardCount())
                return 0;
            Minion m = MakeCopy(card, golden, 0);
            if (!PushToHand(player, m))
                return 0;
            Settle(player);
            return m.uid;
        }

        bool Shop::AddPoolCardToHand(int player, int card, bool golden)
        {
            if (!ValidPlayer(player) || card < 0 || card >= CardCount())
                return false;
            int const pool = OwnPool(player, card);
            if (pool < 0 || m_pool.LeftIn(pool, card) <= 0)
                return false;
            if (!m_pool.Take(card, pool))
                return false;
            Minion m = MakeCopy(card, golden, 1);
            if (!PushToHand(player, m))
            {
                m_pool.Return(card, pool, 1);
                return false;
            }
            Settle(player);
            return true;
        }

        uint32_t Shop::AddRandomToHand(int player, int minTier, int maxTier, uint32_t tribeAny)
        {
            if (!ValidPlayer(player))
                return 0;
            PlayerShop& ps = State(player);
            int const card = m_pool.Draw(m_rng, ps.pools, minTier, maxTier, tribeAny);
            if (card < 0)
                return 0;
            Minion m = MakeCopy(card, false, 1);
            if (!PushToHand(player, m))
            {
                int const pool = OwnPool(player, card);
                if (pool >= 0)
                    m_pool.Return(card, pool, 1);
                return 0;
            }
            Settle(player);
            return m.uid;
        }

        bool Shop::Discover(int player, int minTier, int maxTier, uint32_t tribeAny)
        {
            if (!ValidPlayer(player))
                return false;
            PlayerShop& ps = State(player);
            if (m_pool.CountCards(ps.pools, minTier, maxTier, tribeAny) == 0)
                return false;
            DiscoverRequest req;
            req.minTier = minTier;
            req.maxTier = maxTier;
            req.tribeAny = tribeAny;
            QueueDiscover(player, req);
            return true;
        }

        bool Shop::TakeOffer(int player, int index)
        {
            if (!ValidPlayer(player))
                return false;
            PlayerShop& ps = State(player);
            if (index < 0 || index >= static_cast<int>(ps.offers.size()))
                return false;
            Minion m = ps.offers[static_cast<std::size_t>(index)];
            LeaveTavern(player, m);
            if (!PushToHand(player, m))
                return false;
            ps.offers.erase(ps.offers.begin() + index);
            Settle(player);
            return true;
        }

        bool Shop::ConsumeOffer(int player, int index, Minion& out)
        {
            if (!ValidPlayer(player))
                return false;
            PlayerShop& ps = State(player);
            if (index < 0 || index >= static_cast<int>(ps.offers.size()))
                return false;
            out = ps.offers[static_cast<std::size_t>(index)];
            ps.offers.erase(ps.offers.begin() + index);
            ReturnToPool(player, out);
            return true;
        }

        void Shop::DamageHero(int player, int amount)
        {
            if (!ValidPlayer(player))
                return;
            for (Minion const& m : State(player).board.Items())
            {
                EffectDef const* e = m_effects.Find(m.card);
                if (e && e->heroImmune)
                    return; // Immune (Kathra'natir): no damage from the player's own effects
            }
            TakeDamage(player, amount);   // armor absorbs it first
        }

        void Shop::TriggerBattlecry(int player, uint32_t uid, uint32_t targetUid)
        {
            if (!ValidPlayer(player))
                return;
            int const times = BattlecryTimes(player);
            for (int i = 0; i < times; ++i)
                RunBattlecry(player, uid, targetUid);
            Settle(player);
        }

        void Shop::HeroSummoned(int player, uint32_t uid)
        {
            HeroPowerDef const* const p = Power(player);
            if (!p || p->summonAttack == 0)
                return;
            Minion* m = FindMinion(player, uid);
            if (m && m->IsTribe(p->summonAttackTribe))
                Buff(player, *m, p->summonAttack, 0);
        }

        void Shop::RunBattlecry(int player, uint32_t uid, uint32_t targetUid)
        {
            Minion const* m = FindMinion(player, uid);
            EffectDef const* e = m ? m_effects.Find(m->card) : nullptr;
            if (!e || !e->battlecry)
                return;
            FireOn(player, &EffectDef::battlecry, uid, 0, targetUid);
            FireBoard(player, &EffectDef::onBattlecryTriggered, 0, uid);
        }

        int Shop::BattlecryTimes(int player) const
        {
            if (!ValidPlayer(player))
                return 1;
            // Brann's Blessing makes every Battlecry trigger twice this turn (the printed "twice", so it
            // does not add on top of Brann Bronzebeard).
            int const times = TimesFor(m_effects, State(player).board, TimesKind::Battlecry);
            return State(player).battlecriesTwice ? std::max(2, times) : times;
        }

        int Shop::SummonTimes(int player) const
        {
            return ValidPlayer(player) ? TimesFor(m_effects, State(player).board, TimesKind::Summon) : 1;
        }

        void Shop::SetLastOpponent(int player, Board const& opponent)
        {
            if (!ValidPlayer(player))
                return;
            State(player).lastOpponent = opponent;
            State(player).hasLastOpponent = true;
        }

        void Shop::SetLobbyTribes(int player, std::vector<uint32_t> const& tribes)
        {
            if (ValidPlayer(player))
                State(player).lobbyTribes = tribes;
        }

        std::vector<uint32_t> const& Shop::PlayerLobbyTribes(int player) const
        {
            static std::vector<uint32_t> const none;
            if (!ValidPlayer(player))
                return none;
            PlayerShop const& ps = State(player);
            return ps.lobbyTribes.empty() ? m_lobbyTribes : ps.lobbyTribes;
        }

        void Shop::AddRefreshDiscount(int player, int perRefresh, int charges)
        {
            if (!ValidPlayer(player) || perRefresh <= 0 || charges <= 0)
                return;
            PlayerShop& ps = State(player);
            // "Your next Refresh costs (X) less": one entry per Refresh, used in the order added, so a free Refresh
            // and a (1) less one do not blend and a spent discount leaves nothing behind.
            for (int i = 0; i < charges; ++i)
                ps.refreshDiscounts.push_back(perRefresh);   // the cost floors at 0 in RefreshCost
        }

        void Shop::AddElementalTavernBonus(int player, int attack, int health)
        {
            if (!ValidPlayer(player))
                return;
            PlayerShop& ps = State(player);
            ps.elementalTavernAttack += attack;
            ps.elementalTavernHealth += health;
            RecomputeOfferAuras(player);
        }

        bool Shop::AddRandomOffer(int player, int minTier, int maxTier, uint32_t tribeAny, int excludeCard)
        {
            if (!ValidPlayer(player))
                return false;
            PlayerShop& ps = State(player);
            int const card = m_pool.Draw(m_rng, ps.pools, minTier, maxTier, tribeAny, excludeCard);
            if (card < 0)
                return false;
            ps.offers.push_back(MakeCopy(card, false, 1));
            RecomputeOfferAuras(player);
            return true;
        }

        void Shop::ReduceUpgradeCost(int player, int amount)
        {
            if (!ValidPlayer(player) || amount <= 0)
                return;
            PlayerShop& ps = State(player);
            ps.upgradeCost = std::max(0, ps.upgradeCost - amount);
        }

        uint32_t Shop::AddRandomGoldenToHand(int player, int minTier, int maxTier, uint32_t tribeAny)
        {
            if (!ValidPlayer(player))
                return 0;
            PlayerShop& ps = State(player);
            int const card = m_pool.Draw(m_rng, ps.pools, minTier, maxTier, tribeAny);
            if (card < 0 || CardAt(card).goldenId[0] == '\0')
                return 0;
            Minion m = MakeCopy(card, true, 1);   // the drawn pool copy becomes the Golden minion
            if (!PushToHand(player, m))
            {
                int const pool = OwnPool(player, card);
                if (pool >= 0)
                    m_pool.Return(card, pool, 1);
                return 0;
            }
            Settle(player);
            return m.uid;
        }

        Board const* Shop::LastOpponentBoard(int player) const
        {
            return ValidPlayer(player) && State(player).hasLastOpponent ? &State(player).lastOpponent : nullptr;
        }

        uint32_t Shop::AddRandomToHandExcept(int player, int minTier, int maxTier, uint32_t tribeAny, int excludeCard)
        {
            if (!ValidPlayer(player))
                return 0;
            PlayerShop& ps = State(player);
            int const card = m_pool.Draw(m_rng, ps.pools, minTier, maxTier, tribeAny, excludeCard);
            if (card < 0)
                return 0;
            Minion m = MakeCopy(card, false, 1);
            if (!PushToHand(player, m))
            {
                int const pool = OwnPool(player, card);
                if (pool >= 0)
                    m_pool.Return(card, pool, 1);
                return 0;
            }
            Settle(player);
            return m.uid;
        }

        // Jandice Barov: swap a friendly non-golden minion with a random tavern offer.
        bool Shop::SwapWithRandomOffer(int player, uint32_t uid)
        {
            if (!ValidPlayer(player))
                return false;
            PlayerShop& ps = State(player);
            int const idx = ps.board.IndexOfUid(uid);
            if (idx < 0 || ps.board[idx].golden || ps.offers.empty())
                return false;
            Minion mine = ps.board.Remove(idx);
            int const offer = static_cast<int>(m_rng.Uniform(static_cast<uint32_t>(ps.offers.size())));
            Minion theirs = ps.offers[static_cast<std::size_t>(offer)];
            ps.offers.erase(ps.offers.begin() + offer);
            LeaveTavern(player, theirs);
            ps.board.Insert(idx, theirs);
            StripAura(mine);
            ps.offers.push_back(mine);
            RecomputeOfferAuras(player);
            Settle(player);
            return true;
        }

        // Vol'jin: "Choose two minions. Swap their stats." Each is a minion of the board or of the tavern.
        bool Shop::SwapStatsOf(int player, uint32_t uidA, uint32_t uidB)
        {
            if (!ValidPlayer(player) || uidA == uidB)
                return false;
            PlayerShop& ps = State(player);
            auto const find = [&](uint32_t uid) -> Minion*
            {
                int const idx = ps.board.IndexOfUid(uid);
                if (idx >= 0)
                    return &ps.board[idx];
                for (Minion& o : ps.offers)
                    if (o.uid == uid)
                        return &o;
                return nullptr;
            };
            Minion* const pa = find(uidA);
            Minion* const pb = find(uidB);
            if (!pa || !pb)
                return false;
            Minion& a = *pa;
            Minion& b = *pb;
            int const aAttack = a.attack - a.auraAttack - a.tempAttack;
            int const aHealth = a.maxHealth - a.auraHealth - a.tempHealth;
            int const bAttack = b.attack - b.auraAttack - b.tempAttack;
            int const bHealth = b.maxHealth - b.auraHealth - b.tempHealth;
            a.tempAttack = a.tempHealth = b.tempAttack = b.tempHealth = 0;   // the swapped stats are the new base
            a.bloodGemAttack = a.bloodGemHealth = b.bloodGemAttack = b.bloodGemHealth = 0;   // the gem stats are overwritten
            auto const set = [](Minion& m, int attack, int health)
            {
                m.attack += attack - (m.attack - m.auraAttack);
                m.maxHealth += health - (m.maxHealth - m.auraHealth);
                m.health = m.maxHealth;
            };
            set(a, bAttack, bHealth);
            set(b, aAttack, aHealth);
            RecomputeBoard(player);
            RecomputeOfferAuras(player);
            return true;
        }

        // Malygos / Galakrond: replace a tavern offer with a random pool minion of the tier range.
        bool Shop::ReplaceOffer(int player, int offerIndex, int minTier, int maxTier)
        {
            if (!ValidPlayer(player) || minTier < kMinTier || maxTier > kMaxTier || minTier > maxTier)
                return false;
            PlayerShop& ps = State(player);
            if (offerIndex < 0 || offerIndex >= static_cast<int>(ps.offers.size()))
                return false;
            int const card = m_pool.Draw(m_rng, ps.pools, minTier, maxTier, 0);
            if (card < 0)
                return false;
            Minion old = ps.offers[static_cast<std::size_t>(offerIndex)];
            ps.offers.erase(ps.offers.begin() + offerIndex);
            ReturnToPool(player, old);
            ps.offers.insert(ps.offers.begin() + offerIndex, MakeCopy(card, false, 1));
            RecomputeOfferAuras(player);
            return true;
        }

        // Xyrella / Yogg: take an offer to the hand and change its stats before it can merge (the hand room check is
        // the normal one; a triple may still merge it afterwards).
        bool Shop::TakeOfferAndBuff(int player, int offerIndex, int attack, int health, bool setToStats)
        {
            if (!ValidPlayer(player))
                return false;
            PlayerShop& ps = State(player);
            if (offerIndex < 0 || offerIndex >= static_cast<int>(ps.offers.size()))
                return false;
            Minion m = ps.offers[static_cast<std::size_t>(offerIndex)];
            if (!CanReceive(player, m.card, false))
                return false;
            ps.offers.erase(ps.offers.begin() + offerIndex);
            LeaveTavern(player, m);
            if (setToStats)
            {
                m.attack = attack;
                m.health = m.maxHealth = health;
                m.bloodGemAttack = m.bloodGemHealth = 0;   // the gem stats are overwritten
            }
            else
                m.Buff(attack, health);
            PushToHand(player, m);
            Settle(player);
            return true;
        }

        // Mutanus's Devour takes a board minion away; its pool copies go back.
        bool Shop::RemoveFromBoard(int player, uint32_t uid)
        {
            if (!ValidPlayer(player))
                return false;
            PlayerShop& ps = State(player);
            int const index = ps.board.IndexOfUid(uid);
            if (index < 0)
                return false;
            Minion removed = ps.board.Remove(index);
            ReturnToPool(player, removed);
            RecomputeBoard(player);
            return true;
        }

        // Tess Greymane: the tavern is refilled with copies of the last opponent's warband.
        bool Shop::RefreshWithLastOpponent(int player)
        {
            if (!ValidPlayer(player))
                return false;
            PlayerShop& ps = State(player);
            if (ps.lastOpponent.Empty())
                return false;
            ReturnOffers(player);
            for (Minion const& m : ps.lastOpponent.Items())
            {
                // Base stats of the opponent's card (keeps golden); buffs, hats and gem counters stay behind.
                ps.offers.push_back(MakeCopy(m.card, m.golden, 0));
            }
            RecomputeOfferAuras(player);
            FireQuest(player, QuestEventInfo{ QuestEvent::Refresh });
            FireBoard(player, &EffectDef::onRefresh);
            Settle(player);
            RewardRefresh(player);
            return true;
        }

        // Scabbs Cutterbutter: Discover a plain copy of a minion of the last opponent's warband.
        bool Shop::DiscoverLastOpponent(int player)
        {
            if (!ValidPlayer(player) || State(player).lastOpponent.Empty())
                return false;
            DiscoverRequest req;
            req.boardCopy = true;
            QueueDiscover(player, req);
            return true;
        }

        // Murloc Holmes: a Discover of two minions, one of them the given card of the last opponent's warband.
        void Shop::DiscoverHolmes(int player, int card)
        {
            if (!ValidPlayer(player) || card < 0)
                return;
            DiscoverRequest req;
            req.holmesCard = card;
            QueueDiscover(player, req);
        }

        // Tickatus's Prize Wall (prizeTurn 1-4) and Hand of Fate (-1, any prize).
        bool Shop::PrizeDiscover(int player, int prizeTurn)
        {
            if (!ValidPlayer(player) || DarkmoonPrizeIds(prizeTurn).empty())
                return false;
            DiscoverRequest req;
            req.prizeTurn = prizeTurn;
            QueueDiscover(player, req);
            return true;
        }

        void Shop::AddTavernAttack(int player, int amount)
        {
            if (!ValidPlayer(player) || amount == 0)
                return;
            State(player).tavernAttackAll += amount;   // The Good Stuff
            RecomputeOfferAuras(player);
        }

        void Shop::AddTavernCostDiscount(int player, int amount)
        {
            if (!ValidPlayer(player) || amount <= 0)
                return;
            State(player).tavernCostDiscount += amount;   // Friends and Family Discount
        }

        void Shop::AddExtraTavernOffer(int player)
        {
            if (!ValidPlayer(player))
                return;
            ++State(player).extraOffers;   // New Recruit: one more tavern slot for the rest of the game
            FillOffers(player);
            RecomputeOfferAuras(player);
        }

        void Shop::EnableOpenBar(int player)
        {
            if (!ValidPlayer(player))
                return;
            PlayerShop& ps = State(player);
            ps.openBar = true;   // Open Bar: the first five refreshes of every turn are free
            ps.freeRefreshes = 5;
        }

        void Shop::MarkUnlimitedCoin(int player)
        {
            if (ValidPlayer(player))
                ++State(player).unlimitedCoinsOut;   // The Unlimited Coin comes back at EndTurn
        }

        void Shop::EnableBrannsBlessing(int player)
        {
            if (ValidPlayer(player))
                State(player).battlecriesTwice = true;   // Brann's Blessing: this turn only
        }

        // All That Glitters: a random tavern offer becomes Golden. The two extra copies are taken from the pool when it
        // has them, so a bought or sold Golden keeps the pool count exact.
        bool Shop::MakeOfferGolden(int player, int offerIndex)
        {
            if (!ValidPlayer(player))
                return false;
            PlayerShop& ps = State(player);
            if (offerIndex < 0 || offerIndex >= static_cast<int>(ps.offers.size()))
                return false;
            Minion& o = ps.offers[static_cast<std::size_t>(offerIndex)];
            if (o.golden || o.card < 0 || CardAt(o.card).role == CardRole::Spell || CardAt(o.card).goldenId[0] == '\0')
                return false;
            CardDef const& d = CardAt(o.card);
            o.attack += d.goldenAttack - d.attack;
            o.health += d.goldenHealth - d.health;
            o.maxHealth += d.goldenHealth - d.health;
            uint32_t extra = d.goldenKeywords & ~d.keywords;
            if (extra & kKwMegaWindfury)
                o.keywords &= ~static_cast<uint32_t>(kKwWindfury);
            o.keywords |= extra;
            o.golden = true;
            int const pool = OwnPool(player, o.card);
            for (int i = 0; i < 2; ++i)
                if (pool >= 0 && m_pool.Take(o.card, pool))
                    ++o.poolCopies;
            RecomputeOfferAuras(player);
            return true;
        }

        // Repeat Customer (makeGolden false, +2/+2) and Raise the Stakes (makeGolden true). The minion goes to the hand;
        // a full hand leaves it on the board.
        bool Shop::TakeFriendlyToHand(int player, uint32_t uid, bool makeGolden, int attack, int health, bool nonGoldenOnly)
        {
            if (!ValidPlayer(player))
                return false;
            PlayerShop& ps = State(player);
            int i = ps.board.IndexOfUid(uid);
            if (i < 0 || (nonGoldenOnly && ps.board[i].golden))
                return false;
            if (!CanReceive(player, ps.board[i].card, makeGolden || ps.board[i].golden))
                return false;   // checked before anything changes, so a refused spell leaves the minion as it was
            if (makeGolden && !MakeGolden(player, uid))
                return false;
            i = ps.board.IndexOfUid(uid);
            if (i < 0)
                return false;
            Minion m = ps.board[i];
            m = ps.board.Remove(i);
            if (attack != 0 || health != 0)
                m.Buff(attack, health);
            if (!PushToHand(player, m))
            {
                ps.board.Insert(i, m);
                return false;
            }
            RecomputeBoard(player);
            Settle(player);
            return true;
        }

        // Evolving Tavern: the whole tavern becomes minions of the next Tavern Tier up (capped at 6), as the plan's
        // "replace offers with the next Tier up" reads it.
        void Shop::EvolveTavern(int player)
        {
            if (!ValidPlayer(player))
                return;
            PlayerShop& ps = State(player);
            int const count = static_cast<int>(ps.offers.size());
            ReturnOffers(player);
            int const tier = std::min(kMaxTier, ps.tier + 1);
            for (int i = 0; i < count; ++i)
            {
                int card = m_pool.Draw(m_rng, ps.pools, tier, tier);
                if (card < 0)
                    card = m_pool.Draw(m_rng, ps.pools, kMinTier, tier);   // that Tier is drained: any lower one
                if (card < 0)
                    break;
                ps.offers.push_back(MakeCopy(card, false, 1));
            }
            RecomputeOfferAuras(player);
        }

        // Mindflayer Goggles (also Yogg's Wheel outcome): steal every offer, then refresh the tavern.
        void Shop::StealTavern(int player)
        {
            if (!ValidPlayer(player))
                return;
            PlayerShop& ps = State(player);
            for (int i = static_cast<int>(ps.offers.size()) - 1; i >= 0; --i)
            {
                Minion m = ps.offers[static_cast<std::size_t>(i)];
                ps.offers.erase(ps.offers.begin() + i);
                LeaveTavern(player, m);   // a taken offer loses its tavern bonus, as on a Buy
                if (!PushToHand(player, m))
                    ReturnToPool(player, m);
            }
            FillOffers(player);
            RecomputeOfferAuras(player);
        }

        // Training Session: one of the swap pool's powers (the Nguyen deny list applies).
        bool Shop::DiscoverPower(int player, int count)
        {
            return OfferPowerChoice(player, PowerChoiceKind::Swap, SwapPool(player), count);
        }

        void Shop::CountFriendlyDeaths(int player, int deaths)
        {
            if (!ValidPlayer(player) || deaths <= 0)
                return;
            HeroPowerDef const* const p = Power(player);
            if (!p || p->mechPerDeaths <= 0)
                return;
            PlayerShop& ps = State(player);
            ps.power.friendlyDeaths += deaths;
            while (ps.power.friendlyDeaths >= p->mechPerDeaths)
            {
                ps.power.friendlyDeaths -= p->mechPerDeaths;
                AddRandomToHand(player, kMinTier, ps.tier, kTribeMech);
            }
        }

        // Zephrys: a normal card with exactly two copies on the board and in the hand.
        int Shop::PairForWish(int player) const
        {
            if (!ValidPlayer(player))
                return -1;
            PlayerShop const& ps = State(player);
            std::vector<int> seen;
            auto const check = [&](Minion const& m)
            {
                if (m.card < 0 || m.golden || CardAt(m.card).role == CardRole::Spell ||
                    std::find(seen.begin(), seen.end(), m.card) != seen.end())
                    return -1;
                seen.push_back(m.card);
                if (CardAt(m.card).goldenId[0] == '\0' || NormalCopies(player, m.card, nullptr) != 2)
                    return -1;
                return m.card;
            };
            for (Minion const& m : ps.hand.Items())
            {
                int const card = check(m);
                if (card >= 0)
                    return card;
            }
            for (Minion const& m : ps.board.Items())
            {
                int const card = check(m);
                if (card >= 0)
                    return card;
            }
            return -1;
        }
    }
}
